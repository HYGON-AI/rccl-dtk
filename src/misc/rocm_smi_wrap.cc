/*
Copyright (c) 2021-2024 Advanced Micro Devices, Inc. All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/
#include "rocm_smi_wrap.h"
#include "alt_rsmi.h"
#include "core.h"
#include "utils.h"

#include <dlfcn.h>
#include <mutex>

static int is_wsl2 = -1;

/******************************************************************************
 * Additions on top of the original implementation
 *
 * librocm_smi64 is not linked anymore, it is resolved at runtime with
 * dlopen()/dlsym() (RCCL_ROCM_SMI_LIB overrides the library name/path). The
 * rocm_smi headers stay a build-time dependency, which keeps the RSMI types
 * and the exact signatures in sync (see decltype below). When the library is
 * unavailable, the code keeps its original RCCL_USE_ROCM_SMI_LIB handling and
 * falls back to the internal ARSMI (sysfs) implementation.
 *****************************************************************************/
static void* rsmiHandle = nullptr;

#define RCCL_RSMI_FN(name) static decltype(&name) pfn_##name = nullptr
/* Required: without them the library cannot be used. */
RCCL_RSMI_FN(rsmi_init);
RCCL_RSMI_FN(rsmi_num_monitor_devices);
RCCL_RSMI_FN(rsmi_dev_pci_id_get);
RCCL_RSMI_FN(rsmi_topo_get_link_type);
RCCL_RSMI_FN(rsmi_topo_get_link_weight);
RCCL_RSMI_FN(rsmi_version_get);
RCCL_RSMI_FN(rsmi_status_string);
RCCL_RSMI_FN(rsmi_dev_firmware_version_get);
/* Optional: a missing symbol only disables the matching feature. */
#if defined USE_ROCM_SMI64CONFIG && rocm_smi_VERSION_MAJOR >= 2
RCCL_RSMI_FN(rsmi_minmax_bandwidth_get);
#endif
#ifdef RSMI_SUPPORT_XHCL_TOPO
/* Required as well: the hylink topology cannot be determined without them. */
RCCL_RSMI_FN(rsmi_dev_xhcl_link_number_get);
RCCL_RSMI_FN(rsmi_dev_xhcl_link_state_get);
RCCL_RSMI_FN(rsmi_dev_xhcl_link_remote_bdfid_get);
RCCL_RSMI_FN(rsmi_dev_xhcl_link_remote_dev_type_get);
#endif
#undef RCCL_RSMI_FN

/* Loads librocm_smi64 and resolves the entry points. Runs at most once and the
 * handle is intentionally never closed. */
static ncclResult_t rocm_smi_loadLibrary() {
  static std::once_flag once;
  static ncclResult_t result = ncclSystemError;

  std::call_once(once, []() {
    const char* libs[] = {
      ncclGetEnv("RCCL_ROCM_SMI_LIB"),
      "librocm_smi64.so.2",
      "librocm_smi64.so.1",
      "librocm_smi64.so",
      "librocm_smi.so",
    };
    for (size_t i = 0; i < sizeof(libs) / sizeof(libs[0]); i++) {
      if (libs[i] == nullptr) continue;
      rsmiHandle = dlopen(libs[i], RTLD_NOW | RTLD_LOCAL);
      if (rsmiHandle) {
        INFO(NCCL_INIT, "rocm_smi_lib loaded from %s", libs[i]);
        break;
      }
      INFO(NCCL_INIT, "Could not open %s: %s", libs[i], dlerror());
    }
    if (rsmiHandle == nullptr) {
      WARN("rocm_smi_lib not available");
      return;
    }

#define RCCL_RSMI_LOAD(name)                          \
    do {                                              \
      void* sym = dlsym(rsmiHandle, #name);           \
      if (sym == nullptr) {                           \
        WARN("rocm_smi_lib has no symbol %s", #name); \
        return;                                       \
      }                                               \
      *(void**)&pfn_##name = sym;                     \
    } while (false)
#define RCCL_RSMI_LOAD_OPTIONAL(name) \
    do { *(void**)&pfn_##name = dlsym(rsmiHandle, #name); } while (false)

    RCCL_RSMI_LOAD(rsmi_init);
    RCCL_RSMI_LOAD(rsmi_num_monitor_devices);
    RCCL_RSMI_LOAD(rsmi_dev_pci_id_get);
    RCCL_RSMI_LOAD(rsmi_topo_get_link_type);
    RCCL_RSMI_LOAD(rsmi_topo_get_link_weight);
    RCCL_RSMI_LOAD(rsmi_version_get);
    RCCL_RSMI_LOAD(rsmi_status_string);
    RCCL_RSMI_LOAD(rsmi_dev_firmware_version_get);
#if defined USE_ROCM_SMI64CONFIG && rocm_smi_VERSION_MAJOR >= 2
    RCCL_RSMI_LOAD_OPTIONAL(rsmi_minmax_bandwidth_get);
#endif
#ifdef RSMI_SUPPORT_XHCL_TOPO
    /* Required: the hylink topology cannot be determined without them. */
    RCCL_RSMI_LOAD(rsmi_dev_xhcl_link_number_get);
    RCCL_RSMI_LOAD(rsmi_dev_xhcl_link_state_get);
    RCCL_RSMI_LOAD(rsmi_dev_xhcl_link_remote_bdfid_get);
    RCCL_RSMI_LOAD(rsmi_dev_xhcl_link_remote_dev_type_get);
#endif
#undef RCCL_RSMI_LOAD
#undef RCCL_RSMI_LOAD_OPTIONAL

    result = ncclSuccess;
  });

  return result;
}

/* The entry points are resolved at runtime, hence the null guard. */
#define ROCMSMICHECK(cmd) do {               \
  if (pfn_rsmi_status_string == nullptr)     \
    return ncclInternalError;                \
  rsmi_status_t ret = cmd;                   \
  if( ret != RSMI_STATUS_SUCCESS ) {         \
    const char *err;                         \
    pfn_rsmi_status_string(ret, &err);       \
    WARN("ROCm SMI init failure %s", err);   \
    return ncclInternalError;                \
  }                                          \
} while(false)

#define ARSMICHECK(cmd) do {         \
  int ret = cmd;                     \
  if( ret != 0 ) {		     \
    WARN("ARSMI failure %d", ret);   \
    return ncclInternalError;        \
  }                                  \
} while(false)

RCCL_PARAM(UseRocmSmiLib, "USE_ROCM_SMI_LIB", 1); // Opt-in environment variable for enabling using rocm_smi_lib instead of internal code

ncclResult_t rocm_smi_init() {
  if (__atomic_load_n(&is_wsl2, __ATOMIC_ACQUIRE) == -1)
    __atomic_store_n(&is_wsl2, (access("/dev/dxg", F_OK) == -1) ? 0 : 1, __ATOMIC_RELEASE);
  if (__atomic_load_n(&is_wsl2, __ATOMIC_ACQUIRE)) {
    INFO(NCCL_INIT, "Not using rocm_smi_lib due to WSL2 environment detected.");
    return ncclSuccess;
  }

  if (rcclParamUseRocmSmiLib()) {
    if (rocm_smi_loadLibrary() != ncclSuccess) {
      WARN("rocm_smi_lib unavailable");
      return ncclInternalError;
    }
#ifdef USE_ROCM_SMI_THREAD_ONLY_MUTEX
    ROCMSMICHECK(pfn_rsmi_init(RSMI_INIT_FLAG_THRAD_ONLY_MUTEX));
#else
    ROCMSMICHECK(pfn_rsmi_init(0));
#endif
    rsmi_version_t version;
    ROCMSMICHECK(pfn_rsmi_version_get(&version));
    INFO(NCCL_INIT, "rocm_smi_lib: version %d.%d.%d.%s", version.major, version.minor, version.patch, version.build);
  } else {
    ARSMICHECK(ARSMI_init());
    INFO(NCCL_INIT, "initialized internal alternative rsmi functionality");
  }
  return ncclSuccess;
}

ncclResult_t rocm_smi_getNumDevice(uint32_t* num_devs) {
  if (__atomic_load_n(&is_wsl2, __ATOMIC_ACQUIRE))
    CUDACHECK(cudaGetDeviceCount((int *)num_devs));
  else
    if (rcclParamUseRocmSmiLib()) {
      if (rocm_smi_loadLibrary() != ncclSuccess) return ncclInternalError;
      ROCMSMICHECK(pfn_rsmi_num_monitor_devices(num_devs));
    } else {
      ARSMICHECK(ARSMI_get_num_devices(num_devs));
    }

  return ncclSuccess;
}

ncclResult_t rocm_smi_getDevicePciBusIdString(uint32_t deviceIndex, char* busId, size_t len) {
  uint64_t id;
  if (__atomic_load_n(&is_wsl2, __ATOMIC_ACQUIRE)) {
    CUDACHECK(cudaDeviceGetPCIBusId(busId, len, deviceIndex));
  } else {
    /** rocm_smi's bus ID format
     *  | Name     | Field   |
     *  ---------- | ------- |
     *  | Domain   | [64:32] |
     *  | Reserved | [31:16] |
     *  | Bus      | [15: 8] |
     *  | Device   | [ 7: 3] |
     *  | Function | [ 2: 0] |
     **/
    if (rcclParamUseRocmSmiLib()) {
      if (rocm_smi_loadLibrary() != ncclSuccess) return ncclInternalError;
      ROCMSMICHECK(pfn_rsmi_dev_pci_id_get(deviceIndex, &id));
    } else {
      ARSMICHECK(ARSMI_dev_pci_id_get(deviceIndex, &id));
    }
    snprintf(busId, len, "%04lx:%02lx:%02lx.%01lx", (id) >> 32, (id & 0xff00) >> 8, (id & 0xf8) >> 3, (id & 0x7));
  }
  return ncclSuccess;
}


ncclResult_t rocm_smi_getDeviceIndexByPciBusId(const char* pciBusId, uint32_t* deviceIndex) {
  if (__atomic_load_n(&is_wsl2, __ATOMIC_ACQUIRE)) {
    CUDACHECK(hipDeviceGetByPCIBusId((int *)deviceIndex, pciBusId));
    return ncclSuccess;
  } else {
    uint32_t i, num_devs = 0;
    int64_t busid;

    busIdToInt64(pciBusId, &busid);
    /** convert to rocm_smi's bus ID format
     *  | Name     | Field   |
     *  ---------- | ------- |
     *  | Domain   | [64:32] |
     *  | Reserved | [31:16] |
     *  | Bus      | [15: 8] |
     *  | Device   | [ 7: 3] |
     *  | Function | [ 2: 0] |
     **/
    busid = ((busid&0xffff00000L)<<12)+((busid&0xff000L)>>4)+((busid&0xff0L)>>1)+(busid&0x7L);

    if (rcclParamUseRocmSmiLib()) {
      if (rocm_smi_loadLibrary() != ncclSuccess) return ncclInternalError;
      ROCMSMICHECK(pfn_rsmi_num_monitor_devices(&num_devs));
    } else {
      ARSMICHECK(ARSMI_get_num_devices(&num_devs));
    }
    for (i = 0; i < num_devs; i++) {
      uint64_t bdfid;
      if (rcclParamUseRocmSmiLib()) {
	ROCMSMICHECK(pfn_rsmi_dev_pci_id_get(i, &bdfid));
      } else {
	ARSMICHECK(ARSMI_dev_pci_id_get(i, &bdfid));
      }

      if (bdfid == busid) break;
    }

    if (i < num_devs) {
      *deviceIndex = i;
      return ncclSuccess;
    }
    else {
      if (rcclParamUseRocmSmiLib()) {
	WARN("rocm_smi_lib: %s device index not found", pciBusId);
      } else {
	WARN("ARSMI_lib: %s device index not found", pciBusId);
      }
      return ncclInternalError;
    }
  }
}

ncclResult_t rocm_smi_getLinkInfo(int srcIndex, int dstIndex, RSMI_IO_LINK_TYPE* rsmi_type, int *hops, int *count) {
  if (__atomic_load_n(&is_wsl2, __ATOMIC_ACQUIRE)) {
    *rsmi_type = RSMI_IOLINK_TYPE_PCIEXPRESS;
    *hops = 1;
    *count = 1;
  } else {
    uint64_t rsmi_hops, rsmi_weight;
    *hops = 2;
    *count = 1;

    if (rcclParamUseRocmSmiLib()) {
      if (rocm_smi_loadLibrary() != ncclSuccess) return ncclInternalError;
      ROCMSMICHECK(pfn_rsmi_topo_get_link_type(srcIndex, dstIndex, &rsmi_hops, rsmi_type));
      ROCMSMICHECK(pfn_rsmi_topo_get_link_weight(srcIndex, dstIndex, &rsmi_weight));
      if (*rsmi_type == RSMI_IOLINK_TYPE_XGMI && (rsmi_weight == 15 ||
        rsmi_weight == 41 || rsmi_weight == 13)) {
	uint64_t min_bw = 0, max_bw = 0;
	*hops = 1;
#if defined USE_ROCM_SMI64CONFIG && rocm_smi_VERSION_MAJOR >= 2
	rsmi_version_t version;
	ROCMSMICHECK(pfn_rsmi_version_get(&version));
	if (version.major >= 2 && pfn_rsmi_minmax_bandwidth_get)
	  ROCMSMICHECK(pfn_rsmi_minmax_bandwidth_get(srcIndex, dstIndex, &min_bw, &max_bw));
	if (max_bw && min_bw)
	  *count = max_bw/min_bw;
#endif
      }
    } else {
      ARSMI_linkInfo tinfo;
      ARSMICHECK(ARSMI_topo_get_link_info(srcIndex, dstIndex, &tinfo));

      *rsmi_type  = (RSMI_IO_LINK_TYPE) tinfo.type;
      if (*rsmi_type == RSMI_IOLINK_TYPE_XGMI && (tinfo.weight == 15 ||
        tinfo.weight == 41 || tinfo.weight == 13)) {
	*hops = 1;
	if (tinfo.max_bandwidth && tinfo.min_bandwidth)
	  *count = tinfo.max_bandwidth/tinfo.min_bandwidth;
      }
    }
  }

  return ncclSuccess;
}

/* Same API as the original rccl_wrap.cc helper: MEC firmware version. There is
 * no ARSMI equivalent, so it needs the library, and rsmi_init() has to run
 * before the query (the original implementation did the same). */
ncclResult_t rocm_smi_getFirmwareVersion(uint32_t deviceIndex, uint64_t* fwVersion) {
  if (fwVersion == nullptr) return ncclInvalidArgument;
  if (rocm_smi_loadLibrary() != ncclSuccess) return ncclInternalError;
#ifdef USE_ROCM_SMI_THREAD_ONLY_MUTEX
  ROCMSMICHECK(pfn_rsmi_init(RSMI_INIT_FLAG_THRAD_ONLY_MUTEX));
#else
  ROCMSMICHECK(pfn_rsmi_init(0));
#endif
  rsmi_status_t ret = pfn_rsmi_dev_firmware_version_get(deviceIndex, RSMI_FW_BLOCK_MEC, fwVersion);
  if (ret != RSMI_STATUS_SUCCESS) return ncclInternalError;
  return ncclSuccess;
}

#ifdef RSMI_SUPPORT_XHCL_TOPO
/* The XHCL (hylink) topology API only exists in the vendor rocm_smi_lib and is
 * required (see rocm_smi_loadLibrary), which reports why it is unavailable. */
ncclResult_t rocm_smi_dev_xhcl_link_number_get(uint32_t deviceIndex, uint32_t *linkNum) {
  if (rocm_smi_loadLibrary() != ncclSuccess) return ncclInternalError;
  ROCMSMICHECK(pfn_rsmi_dev_xhcl_link_number_get(deviceIndex, linkNum));
  return ncclSuccess;
}

ncclResult_t rocm_smi_dev_xhcl_link_state_get(uint32_t deviceIndex, uint32_t linkId, uint32_t *linkState) {
  if (rocm_smi_loadLibrary() != ncclSuccess) return ncclInternalError;
  ROCMSMICHECK(pfn_rsmi_dev_xhcl_link_state_get(deviceIndex, linkId, linkState));
  return ncclSuccess;
}

ncclResult_t rocm_smi_dev_xhcl_link_remote_bdfid_get(uint32_t deviceIndex, uint32_t linkId, uint64_t *pciBusId, bool *isSkip) {
  if (rocm_smi_loadLibrary() != ncclSuccess) return ncclInternalError;
  if (RSMI_STATUS_SUCCESS != pfn_rsmi_dev_xhcl_link_remote_bdfid_get(deviceIndex, linkId, pciBusId)) {
    WARN("rsmi_dev_xhcl_link_remote_bdfid_get failed. Use 0 for pciBusId and true for isSkip.");
    if (isSkip) {
      *pciBusId = 0;
      *isSkip = true;
    }
  }
  return ncclSuccess;
}

ncclResult_t rocm_smi_dev_xhcl_link_remote_dev_type_get(uint32_t deviceIndex, uint32_t linkId, RSMI_XHCL_LINK_TYPE *devType) {
  if (rocm_smi_loadLibrary() != ncclSuccess) return ncclInternalError;
  ROCMSMICHECK(pfn_rsmi_dev_xhcl_link_remote_dev_type_get(deviceIndex, linkId, devType));
  return ncclSuccess;
}
#endif
