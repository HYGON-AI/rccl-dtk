/*
 *Copyright 2016-2025 by HCSW Group.
 * All right reserved. See COPYRIGHT for detailed Information.
 */

#ifndef INCLUDE_ROCM_SMI_MKFD_IOCTL_H_
#define INCLUDE_ROCM_SMI_MKFD_IOCTL_H_

#include <linux/types.h>
#include <linux/ioctl.h>

/*
* Note: There are two duplicate definitions for kfd_process_run_mode
* and kfd_ioctl_set_process_mode_args, one in kfd_ioctl.h and 
* the other in mkfd_ioctl.h. This is because UMD needs to call the 
* interface from both kfd and mkfd paths, and the UMD management process
* and business process each contain their own header files, without 
* a common header file; Therefore, drivers must be careful when changing
* this definition, and changes on one side must be synchronized with
* changes on the other side to avoid inconsistent definitions on both sides
*/

#define MAX_KFD_NODES 256
#define PATH_MAX 256
#define REG_SOCKET_ID 0x5A08C

struct mkfd_ioctl_regs_op_args {
	__u32 gpu_id; /* to KFD */
	bool read;
	bool pm_pg_lock;
	bool use_bank;
	bool use_ring;
	__u32 se_bank;
	__u32 sh_bank;
	__u32 instance_bank;
	__u32 me;
	__u32 pipe;
	__u32 queue;
	__u32 vmid;
	__u32 reg;
	__u32 value;
};

#define MKFD_IOCTL_BASE 'M'

#define MKFD_IOWR(nr, type) _IOWR(MKFD_IOCTL_BASE, nr, type)

#define MKFD_IOC_REGS_OP   \
            MKFD_IOWR(0x18, struct mkfd_ioctl_regs_op_args)

#endif // INCLUDE_ROCM_SMI_MKFD_IOCTL_H_