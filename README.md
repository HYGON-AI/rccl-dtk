# RCCL

ROCm Communication Collectives Library

This repository provides HCU adaptations of RCCL maintained by Hygon Information Technology Co., Ltd.
This README describes the **`dtk-26.04`** branch, with RCCL API version **2.22.3**.

> **Documentation:** Sources for this branch are in [docs](docs). The [upstream RCCL documentation](https://rocm.docs.amd.com/projects/rccl/en/latest/index.html) describes upstream releases; use the fixed upstream baseline below when comparing versions.

## Introduction

RCCL (pronounced "Rickle") is a stand-alone library of standard collective communication routines for GPUs, implementing all-reduce, all-gather, reduce, broadcast, reduce-scatter, gather, scatter, and all-to-all. There is also initial support for direct GPU-to-GPU send and receive operations.  It has been optimized to achieve high bandwidth on platforms using PCIe, HSL as well as networking using InfiniBand Verbs or TCP/IP sockets. RCCL supports an arbitrary number of GPUs installed in a single node or multiple nodes, and can be used in either single- or multi-process (e.g., MPI) applications.

The collective operations are implemented using ring and tree algorithms and have been optimized for throughput and latency. For best performance, small operations can be either batched into larger operations or aggregated through the API.

## Requirements

1. Hygon HCUs supported by the DTK release for this branch.
2. A compatible DTK installation providing the HIP runtime and HIP compiler.

## Quickstart RCCL Build

RCCL directly depends on the HIP runtime and HIP compiler supplied by DTK.
Before building, load your DTK environment, replacing the example path with your installation:

```shell
source /path/to/dtk/env.sh
```

The root of this repository has a helper script `install.sh` to build and install RCCL with a single command. It hard-codes configurations that can be specified through invoking cmake directly, but it's a great way to get started quickly and can serve as an example of how to build/install RCCL.

### To build the library using the install script:

```shell
./install.sh
```

For the options supported by this branch, run:

```shell
./install.sh --help
```

By default, RCCL builds for all GPU targets defined in `DEFAULT_GPUS` in `CMakeLists.txt`. To target specific GPU(s), and potentially reduce build time, use `--gpu-targets` as a `;` separated string listing GPU(s) to target.

## Manual build

### To build the library using CMake:

```shell
$ git clone --recursive --branch dtk-26.04 https://github.com/HYGON-AI/rccl-dtk.git
$ cd rccl-dtk
$ mkdir build
$ cd build
$ cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER="$ROCM_PATH/bin/hipcc" \
    -DCMAKE_CXX_COMPILER="$ROCM_PATH/bin/hipcc" \
    -DCMAKE_PREFIX_PATH="$ROCM_PATH;/opt/hyhal"
$ make -j 16      # Or some other suitable number of parallel jobs
```
If you have already cloned, you can checkout the external submodules manually.
```shell
$ git submodule update --init --recursive --depth=1
```
You may substitute an installation path of your own choosing by passing `CMAKE_INSTALL_PREFIX`. For example:
```shell
$ cmake -DCMAKE_INSTALL_PREFIX=$PWD/rccl-install -DCMAKE_BUILD_TYPE=Release ..
```
Ensure `rocm-cmake` is available from your DTK installation or build environment. When CMake cannot locate DTK packages automatically, pass `-DCMAKE_PREFIX_PATH="$ROCM_PATH;/opt/hyhal"`.

### To build the RCCL package and install package :

After building the library, run the following commands from the parent directory of the checkout:

```shell
$ cd rccl-dtk/build
$ make package
$ sudo dpkg -i *.deb
```

Installing a system-wide package requires sudo/root access. You can also use RCCL directly by including the path containing `librccl.so`, or select an installation location with `CMAKE_INSTALL_PREFIX`.

## Docker build

The [docker](docker) directory contains inherited upstream ROCm container examples.
The native build instructions above target this HCU/DTK branch.

## Tests

There are rccl unit tests implemented with the Googletest framework in RCCL.  The rccl unit tests require Googletest 1.10 or higher to build and execute properly (installed with the -d option to install.sh).
To invoke the rccl unit tests, go to the build folder, then the test subfolder, and execute the appropriate rccl unit test executable(s).

rccl unit test names are now of the format:

    CollectiveCall.[Type of test]

Filtering of rccl unit tests should be done with environment variable and by passing the `--gtest_filter` command line flag, for example:

```shell
UT_DATATYPES=ncclBfloat16 UT_REDOPS=prod ./rccl-UnitTests --gtest_filter="AllReduce.C*"
```

will run only AllReduce correctness tests with bfloat16 datatype. A list of available filtering environment variables appears at the top of every run. See "Running a Subset of the Tests" at https://google.github.io/googletest/advanced.html#running-a-subset-of-the-tests for more information on how to form more advanced filters.

There are also other performance and error-checking tests for RCCL.  These are maintained separately at https://github.com/ROCm/rccl-tests.
See the rccl-tests README for more information on how to build and run those tests.

## Library and API Documentation

See [docs](docs) for the documentation in this branch and the [fixed upstream documentation](https://github.com/ROCm/rccl/tree/7b86f83d8468a577ed59d9b2a34a8d7639d36072/docs) for its upstream baseline.

### How to build documentation

Run the steps below to build documentation locally.

```shell
cd docs
pip3 install -r sphinx/requirements.txt
python3 -m sphinx -T -E -b html -d _build/doctrees -D language=en . _build/html
```

## Upstream Attribution

This DTK branch is based on the following fixed upstream baseline:

- **Upstream project**: RCCL (ROCm Communication Collectives Library)
- **Upstream repository**: [https://github.com/ROCm/rccl](https://github.com/ROCm/rccl)
- **Upstream branch**: [`release/rocm-rel-6.4`](https://github.com/ROCm/rccl/tree/release/rocm-rel-6.4)
- **Upstream tag**: [`rocm-6.4.0`](https://github.com/ROCm/rccl/tree/rocm-6.4.0)
- **Upstream commit**: [`7b86f83d8468a577ed59d9b2a34a8d7639d36072`](https://github.com/ROCm/rccl/commit/7b86f83d8468a577ed59d9b2a34a8d7639d36072)
- **Upstream license**: BSD-3-Clause; additional third-party notices and license terms are retained in [LICENSE.txt](LICENSE.txt) and [NOTICES.txt](NOTICES.txt).

The commit above is the immutable upstream baseline for this branch; later HCU/DTK changes build on it.

HCU adaptations, modifications, and original contributions by Hygon Information Technology Co., Ltd. are licensed under the BSD-3-Clause License.

**Modified by Hygon Information Technology Co., Ltd.** Original copyright notices and license terms from the upstream RCCL project are retained.

See [LICENSE.txt](LICENSE.txt) and [NOTICES.txt](NOTICES.txt) for details.
