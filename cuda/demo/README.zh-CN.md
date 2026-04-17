
# CUDA demo 编译与环境配置说明（Ubuntu 24.04）

本目录提供多个 CUDA demo，包括 `demo.cu`、`streaming_demo.cu` 以及拆分的 `warp_reduce_add` demo 源文件，并详细说明：

- 如何在 Ubuntu 24.04 下配置 clang 18.1.3 和 CUDA Toolkit 12.x 环境
- 如何编译这些 CUDA demo 源码并生成可执行程序和 PTX 文件
- 如何在 ProtoGPU 上运行 PTX 6.4/sm_70 并验证仿真
- 常见问题与排查建议
- 相关构建文档和官方参考资料链接

适用于希望用 clang+CUDA 工具链配合 ProtoGPU，对本目录中的各个 demo 进行 PTX 仿真的开发者。

## 1. 环境准备

> **说明：** clang 和 CUDA Toolkit 有多种安装方式（如 apt、conda、runfile、tar 包、源码编译等），请根据实际环境和需求选择。下述仅以一种常见方式举例，实际路径和版本请以本机为准。

### 1.1 安装 clang 18.1.3

（示例：apt/LLVM 官网脚本安装）

```bash
# 添加 LLVM apt 源（如未添加）
wget https://apt.llvm.org/llvm.sh
chmod +x llvm.sh
sudo ./llvm.sh 18

# 检查版本
clang++ --version
# 应输出 18.1.3
```

### 1.2 安装 CUDA Toolkit 12.x

（示例：NVIDIA runfile 离线包安装）

1. 下载 runfile 安装包（[NVIDIA 官网](https://developer.nvidia.com/cuda-toolkit-archive)）
2. 安装：

```bash
sudo sh cuda_12.3.0_*.run
# 只装 Toolkit，不装驱动
# 按提示设置环境变量
export PATH=/usr/local/cuda/bin:$PATH
export LD_LIBRARY_PATH=/usr/local/cuda/lib64:$LD_LIBRARY_PATH
```

3. 验证头文件和库：

```bash
ls /usr/local/cuda/include/cuda_runtime.h
ls /usr/local/cuda/lib64/libcudart.so
```

## 2. 编译这些 CUDA demo .cu 源文件

### 2.1 编译 demo.cu

#### 2.1.1 生成可执行程序


```bash
clang++ demo.cu -o demo \
  --cuda-path=/usr/local/cuda \
  --cuda-gpu-arch=sm_70 \
  -L/usr/local/cuda/lib64 -lcudart \
  -I/usr/local/cuda/include
```

- `--cuda-path` 指定 CUDA Toolkit 路径（如 /usr/local/cuda，/usr/local/cuda 通常是执行具体 cuda 版本的软连接）
- `--cuda-gpu-arch` 目标 GPU 架构（如 sm_70）
- `-lcudart` 链接 CUDA runtime



#### 2.1.2 生成 PTX 文件


```bash
clang++ demo.cu -o demo.ptx \
  --cuda-path=/usr/local/cuda \
  --cuda-gpu-arch=sm_70 \
  --cuda-device-only \
  --cuda-feature=+ptx64 \
  -I/usr/local/cuda/include
```

- `--cuda-device-only` 仅生成 device 代码（PTX）
- `--cuda-feature=+ptx64` 启用 64-bit PTX（通常会输出 `.address_size 64`）
- 另外一种指定输出的 PTX 版本的选项 `--cuda-ptx-version`，但 clang 18.1.3 **不支持**

可以通过查看 PTX 头部确认版本与架构：

```bash
head -n 20 demo.ptx
```

通常应看到类似：

- `.version 6.4`
- `.target sm_70`
- `.address_size 64`

---

### 2.2 编译 streaming_demo.cu（streams + memcpyAsync + kernel）

本 repo 还提供一个更贴近“streaming”使用方式的 CUDA C demo：
- `cuda/demo/streaming_demo.cu`

它会创建两个 stream，并在各自 stream 上做：H2D -> kernel -> D2H，然后验证结果并输出 `OK`。

#### 2.2.1 编译可执行程序

```bash
clang++ streaming_demo.cu -o streaming_demo \
  --cuda-path=/usr/local/cuda \
  --cuda-gpu-arch=sm_70 \
  -L/usr/local/cuda/lib64 -lcudart \
  -I/usr/local/cuda/include
```

#### 2.2.2 生成 PTX 文件（用于 shim 的 PTX override）

```bash
clang++ streaming_demo.cu -S -o streaming_demo.ptx \
  --cuda-path=/usr/local/cuda \
  --cuda-gpu-arch=sm_70 \
  --cuda-device-only \
  --cuda-feature=+ptx64 \
  -I/usr/local/cuda/include
```

说明：
- 当前 CUDA Runtime shim 的 fatbin→PTX 提取仍是 MVP 级别（不同工具链可能把 PTX 以 tokenized/encoded 形式放进 fatbin），
  因此运行这个 demo 时建议用 `GPUSIM_CUDART_SHIM_PTX_OVERRIDE` 显式提供文本 PTX。
- 在 Linux/WSL 上，`GPUSIM_CUDART_SHIM_PTX_OVERRIDE` 可以是单个 PTX 路径，也可以是用 `:` 分隔的 PTX 路径列表。shim 会按顺序搜索这些 PTX，命中请求 `.entry` 的第一个 PTX 即为最终选择。
- `-S` 用于强制输出**文本 PTX 汇编**；否则 clang 在某些版本/参数组合下可能把输出当作目标文件或其它中间产物，导致 `*.ptx` 不是可读的 PTX 文本。

#### 2.2.3 使用 CUDA Runtime shim 运行（Linux/WSL）

在仓库根目录先构建 shim（得到 `build/libcudart.so.12`），然后把 shim 放到动态加载器优先路径，并设置 PTX override：

```bash
# repo root
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build

export LD_LIBRARY_PATH="$PWD/build:${LD_LIBRARY_PATH}"
export GPUSIM_CUDART_SHIM_PTX_OVERRIDE="$PWD/cuda/demo/streaming_demo.ptx"

./cuda/demo/streaming_demo
```

Linux/WSL 下的多 PTX 示例：

```bash
export GPUSIM_CUDART_SHIM_PTX_OVERRIDE="$PWD/cuda/demo/streaming_demo_a.ptx:$PWD/cuda/demo/streaming_demo_b.ptx"
```

如果显式设置了 override 环境变量，而列表中的任一 PTX 文件缺失、为空或不是合法 PTX 文本，shim 会立即失败，并且不会回退到 fatbin 提取。

---

### 2.3 编译 warp_reduce_add demo 源文件（可执行与 PTX 源分离）

对于自定义 PTX 指令（例如 `warp_reduce_add`），建议使用“可执行源 + PTX 源分离”方式：

- Host 可执行程序源码：`cuda/demo/warp_reduce_add_demo_alternative.cu`
- PTX override 源码：`cuda/demo/warp_reduce_add_demo_ptx.cu`

编译 host 可执行程序：

```bash
clang++ warp_reduce_add_demo_alternative.cu -o warp_reduce_add_demo \
  --cuda-path=/usr/local/cuda \
  --cuda-gpu-arch=sm_70 \
  -L/usr/local/cuda/lib64 -lcudart \
  -I/usr/local/cuda/include
```

生成 PTX override 文本：

```bash
clang++ warp_reduce_add_demo_ptx.cu -S -o warp_reduce_add_demo.ptx \
  --cuda-path=/usr/local/cuda \
  --cuda-gpu-arch=sm_70 \
  --cuda-device-only \
  --cuda-feature=+ptx64 \
  -I/usr/local/cuda/include
```

通过 shim 运行：

```bash
export LD_LIBRARY_PATH="$PWD/build:${LD_LIBRARY_PATH}"
export GPUSIM_CUDART_SHIM_PTX_OVERRIDE="$PWD/cuda/demo/warp_reduce_add_demo.ptx"
./cuda/demo/warp_reduce_add_demo
```

这样分离的原因：常规 host CUDA 编译路径会调用 `ptxas`，它无法接受未知自定义指令；把自定义指令放在独立的 device-only PTX override 中，可以同时保持 host 编译兼容性和 ProtoGPU 端到端验证能力。

---

## 3. 在 ProtoGPU 上运行 PTX 6.4/sm_70

假设已编译出 demo.ptx，且已准备好对应的资产文件：

> **注：** `build/gpu-sim-cli` 的编译方法详见 [../../docs/doc_build/build.zh-CN.md](../../docs/doc_build/build.zh-CN.md)。

建议在**仓库根目录**执行（路径与下面命令一致）：

```bash
./build/gpu-sim-cli \
  --ptx cuda/demo/demo.ptx \
  --ptx-isa assets/ptx_isa/demo_ptx64.json \
  --inst-desc assets/inst_desc/demo_desc.json \
  --config assets/configs/demo_config.json \
  --entry add_kernel \
  --grid 2,1,1 \
  --block 128,1,1 \
  --trace out/demo.trace.jsonl \
  --stats out/demo.stats.json
```

- `--entry` 指定 kernel 名称（与 demo.cu 保持一致）
- `--grid`/`--block` 对应 kernel 启动参数（与 host 代码一致）
- 其余参数请根据实际资产路径调整

如果出现 `entry not found` 之类错误，请打开 PTX 文件查找 `.entry ...` 的真实入口名（可能是 C++ name-mangling 形式，例如 `_Z10add_kernelPKjS0_Pjj`），并用该名字替换 `--entry`。

### 3.1 关于 `.param`（ld.param）与参数注入

`demo.ptx` 里的 kernel 入口是带参数的（3 个指针 + 1 个 u32）。如果用上面的“单 kernel 模式”直接跑，当前实现**不会**自动注入参数 blob，因此会在第一条 `ld.param.*` 处报错（例如 `E_PARAM_MISS`）。

要让 `add_kernel` 真正执行，需要通过 **workload 模式**为 device buffer 分配内存、做 H2D/D2H copy，并打包 kernel 参数：

```bash
./build/gpu-sim-cli \
  --config assets/configs/demo_config.json \
  --workload cuda/demo/add_kernel_workload.json \
  --trace out/demo.trace.jsonl \
  --stats out/demo.stats.json
```

说明：
- workload 文件会分配 3 个 device buffer（A/B/C，各 256 个 u32），并把参数名（例如 `_Z10add_kernelPKjS0_Pjj_param_0`）映射到这些 buffer 的 device pointer。
- 该 workload 里 A/B 初始是 zeros，因此 C 预期也是 zeros；当前主要用于验证“能跑通参数 + global memory + 控制流”。

仿真输出如无报错，说明 PTX 及资产配置正确。

---


## 4. 常见问题

- clang 18.x 仅支持 CUDA Toolkit ≤ 12.3，13.x 及以上头文件不兼容。
- 如遇找不到头文件（如 `texture_fetch_functions.h`），请确认 CUDA Toolkit 路径和版本（如 /usr/local/cuda）。
- 建议全程使用同一版本 clang 和 CUDA，避免混用系统自带 nvcc/clang。
- 若 ProtoGPU 报资产缺失或指令不支持，请补齐对应 JSON 文件。
- clang 18 某些版本不支持 `--cuda-ptx-version`，如遇报错请移除该参数。

---

## 5. 参考
- [LLVM 官方文档](https://releases.llvm.org/)
- [NVIDIA CUDA Toolkit Archive](https://developer.nvidia.com/cuda-toolkit-archive)
- [Clang CUDA 支持](https://clang.llvm.org/docs/UsersManual.html#cuda)
