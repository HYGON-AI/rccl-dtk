# build_ut.sh 使用说明

## 功能

`build_ut.sh` 用于分阶段构建仓库内 `googletest` 和 `RCCL`：

1. 构建并安装 `googletest`
2. 构建 `RCCL`
3. 当 `BUILD_TESTS=ON` 时，显式构建 `rccl-UnitTests`

## 前置条件

1. 已安装 `cmake`
2. 已安装 ROCm
3. 已设置环境变量 `ROCM_PATH`
4. 在仓库根目录执行脚本

示例：

```bash
export ROCM_PATH=/opt/rocm
```

## 默认行为

脚本固定：

- `BUILD_TYPE=Release`
- `JOBS=$(nproc)`
- `STAGE=all`

目录固定为：

- gtest 临时构建目录：`build/.gtest-build`
- gtest 安装目录：`build/gtest-install`
- rccl 构建目录：`build/`
- UT 二进制：`build/test/rccl-UnitTests`

## 参数

支持以下参数：

- `-t, --build-tests ON|OFF`
  - 控制 `BUILD_TESTS`，默认 `ON`

- `-f, --full-stress ON|OFF`
  - 控制 `FULL_STRESS_TEST`，默认 `OFF`
  - 当 `BUILD_TESTS=OFF` 时自动强制为 `OFF`

- `-s, --stage all|gtest|rccl`
  - `all`：先构建 gtest，再构建 rccl（默认）
  - `gtest`：只构建 gtest
  - `rccl`：只构建 rccl（要求已有 `build/gtest-install`）

- `-h, --help`
  - 输出帮助信息

## 常用命令

完整构建：

```bash
./build_ut.sh
```

只构建 gtest：

```bash
./build_ut.sh -s gtest
```

只构建 rccl：

```bash
./build_ut.sh -s rccl
```

关闭 UT：

```bash
./build_ut.sh -t OFF
```

开启 FULL_STRESS_TEST：

```bash
./build_ut.sh -t ON -f ON
```

## 清理策略

脚本会在构建前自动清理旧产物：

1. 构建 gtest 前删除：
   - `build/.gtest-build`
   - `build/gtest-install`
2. gtest 安装完成后再次删除 `build/.gtest-build`
3. 构建 rccl 前清理 `build/` 下旧 rccl 产物，但保留 `build/gtest-install`

## 常见报错

`Error: ROCM_PATH is not set`
- 先执行 `export ROCM_PATH=/opt/rocm`

`Error: ROCm path does not exist`
- 检查 `ROCM_PATH` 是否正确

`Error: GoogleTest install dir not found`
- 先执行 `./build_ut.sh -s gtest`
- 或直接执行 `./build_ut.sh -s all`

`Error: rccl-UnitTests target built but binary not found`
- 确认使用了 `-t ON`
- 检查 `build/test/` 下编译输出