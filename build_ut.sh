#!/usr/bin/env bash

# Copyright (c) 2026 Hygon Information Technology Co., Ltd.
# SPDX-License-Identifier: BSD-3-Clause

set -euo pipefail

ROCM_PATH="${ROCM_PATH:-}"
BUILD_TESTS="ON"
FULL_STRESS_TEST="OFF"
BUILD_TYPE="Release"
JOBS="$(nproc)"
STAGE="all" # all|gtest|rccl
ONLY_FUNCS="${ONLY_FUNCS:-}"

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${ROOT_DIR}/build"
GTEST_SRC_DIR="${ROOT_DIR}/tools/googletest"
GTEST_BUILD_DIR="${BUILD_DIR}/.gtest-build"
GTEST_INSTALL_DIR="${BUILD_DIR}/gtest-install"
RCCL_BUILD_DIR="${BUILD_DIR}"

show_help() {
  cat <<EOF
Usage: $0 [options]

Options:
  -t, --build-tests ON|OFF     Enable/disable unit tests in RCCL (default: ON)
  -f, --full-stress ON|OFF     Enable/disable FULL_STRESS_TEST (default: OFF)
  -s, --stage STAGE            Build stage: all | gtest | rccl (default: all)
  -o, --only-funcs FUNCS        Pass -DONLY_FUNCS=FUNCS when configuring RCCL (default: full build)
  -h, --help                   Show this help message
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    -t|--build-tests)
      BUILD_TESTS="$2"
      shift 2
      ;;
    -f|--full-stress)
      FULL_STRESS_TEST="$2"
      shift 2
      ;;
    -s|--stage)
      STAGE="$2"
      shift 2
      ;;
    -o|--only-funcs)
      ONLY_FUNCS="$2"
      shift 2
      ;;
    -h|--help)
      show_help
      exit 0
      ;;
    *)
      echo "Unknown option: $1"
      show_help
      exit 1
      ;;
  esac
done

if [[ -z "${ROCM_PATH}" ]]; then
  echo "Error: ROCM_PATH is not set"
  echo "Please export ROCM_PATH first, for example: export ROCM_PATH=/opt/rocm"
  exit 1
fi

if [[ ! -d "${ROCM_PATH}" ]]; then
  echo "Error: ROCm path does not exist: ${ROCM_PATH}"
  exit 1
fi

if [[ ! -d "${GTEST_SRC_DIR}" || -z "$(find "${GTEST_SRC_DIR}" -mindepth 1 -maxdepth 1 -print -quit)" ]]; then
  echo '请通过命令`git submodule update --init ./tools/googletest/`加载 googletest 源码后进行编译'
  exit 1
fi

if [[ "${BUILD_TESTS}" != "ON" && "${BUILD_TESTS}" != "OFF" ]]; then
  echo "Error: BUILD_TESTS must be ON or OFF"
  exit 1
fi

if [[ "${FULL_STRESS_TEST}" != "ON" && "${FULL_STRESS_TEST}" != "OFF" ]]; then
  echo "Error: FULL_STRESS_TEST must be ON or OFF"
  exit 1
fi

if [[ "${STAGE}" != "all" && "${STAGE}" != "gtest" && "${STAGE}" != "rccl" ]]; then
  echo "Error: STAGE must be one of all|gtest|rccl"
  exit 1
fi

if [[ "${BUILD_TESTS}" == "OFF" ]]; then
  FULL_STRESS_TEST="OFF"
fi

clean_dir_if_exists() {
  local dir="$1"
  local name="$2"
  if [[ -e "${dir}" ]]; then
    echo "==> Remove previous ${name} artifacts: ${dir}"
    rm -rf "${dir}"
  fi
}

clean_rccl_build() {
  if [[ -d "${RCCL_BUILD_DIR}" ]]; then
    echo "==> Remove previous RCCL build artifacts (keep gtest install): ${RCCL_BUILD_DIR}"
    local entry base
    shopt -s nullglob dotglob
    for entry in "${RCCL_BUILD_DIR}"/*; do
      base="$(basename "${entry}")"
      if [[ "${base}" == "gtest-install" ]]; then
        continue
      fi
      rm -rf "${entry}"
    done
    shopt -u nullglob dotglob
  fi
}

build_gtest() {
  clean_dir_if_exists "${GTEST_BUILD_DIR}" "GoogleTest build"
  clean_dir_if_exists "${GTEST_INSTALL_DIR}" "GoogleTest install"

  echo "==> [1/2] Configure GoogleTest"
  cmake -S "${GTEST_SRC_DIR}" -B "${GTEST_BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -DCMAKE_INSTALL_PREFIX="${GTEST_INSTALL_DIR}" \
    -DBUILD_GMOCK=OFF \
    -Dgtest_build_tests=OFF \
    -Dgtest_force_shared_crt=ON

  echo "==> [1/2] Build GoogleTest"
  cmake --build "${GTEST_BUILD_DIR}" --parallel "${JOBS}"

  echo "==> [1/2] Install GoogleTest"
  cmake --install "${GTEST_BUILD_DIR}"

  # Keep only artifacts needed by RCCL (installed gtest); remove gtest intermediate build tree.
  if [[ -d "${GTEST_BUILD_DIR}" ]]; then
    echo "==> [1/2] Remove GoogleTest intermediate build dir: ${GTEST_BUILD_DIR}"
    rm -rf "${GTEST_BUILD_DIR}"
  fi
}

build_rccl() {
  clean_rccl_build

  if [[ ! -d "${GTEST_INSTALL_DIR}" ]]; then
    echo "Error: GoogleTest install dir not found: ${GTEST_INSTALL_DIR}"
    echo "Run stage 'gtest' first, or use '-s all'."
    exit 1
  fi

  echo "==> [2/2] Configure RCCL"
  local cmake_args=(
    -S "${ROOT_DIR}"
    -B "${RCCL_BUILD_DIR}"
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"
    -DCMAKE_PREFIX_PATH="${ROCM_PATH};${GTEST_INSTALL_DIR}"
    -DGTEST_PATH="${GTEST_INSTALL_DIR}"
    -DBUILD_TESTS="${BUILD_TESTS}"
    -DFULL_STRESS_TEST="${FULL_STRESS_TEST}"
  )
  if [[ -n "${ONLY_FUNCS}" ]]; then
    cmake_args+=("-DONLY_FUNCS=${ONLY_FUNCS}")
  fi
  printf '==> RCCL configure command: cmake'
  printf ' %q' "${cmake_args[@]}"
  printf '\n'
  cmake "${cmake_args[@]}"

  echo "==> [2/2] Build RCCL"
  cmake --build "${RCCL_BUILD_DIR}" --parallel "${JOBS}"

  if [[ "${BUILD_TESTS}" == "ON" ]]; then
    echo "==> [2/2] Build rccl-UnitTests target explicitly"
    cmake --build "${RCCL_BUILD_DIR}" --target rccl-UnitTests --parallel "${JOBS}"

    local ut_bin="${RCCL_BUILD_DIR}/test/rccl-UnitTests"
    if [[ -x "${ut_bin}" ]]; then
      echo "rccl-UnitTests generated: ${ut_bin}"
    else
      echo "Error: rccl-UnitTests target built but binary not found at ${ut_bin}"
      exit 1
    fi
  fi
}

echo "Build configuration:"
echo "  ROCM_PATH=${ROCM_PATH}"
echo "  BUILD_TESTS=${BUILD_TESTS}"
echo "  FULL_STRESS_TEST=${FULL_STRESS_TEST}"
echo "  BUILD_TYPE=${BUILD_TYPE}"
echo "  JOBS=${JOBS}"
echo "  STAGE=${STAGE}"
echo "  ONLY_FUNCS=${ONLY_FUNCS:-<full build>}"
echo "  BUILD_DIR=${BUILD_DIR}"
echo "  GTEST_BUILD_DIR=${GTEST_BUILD_DIR}"
echo "  GTEST_INSTALL_DIR=${GTEST_INSTALL_DIR}"
echo "  RCCL_BUILD_DIR=${RCCL_BUILD_DIR}"

case "${STAGE}" in
  all)
    build_gtest
    build_rccl
    ;;
  gtest)
    build_gtest
    ;;
  rccl)
    build_rccl
    ;;
esac

echo "Build done."
