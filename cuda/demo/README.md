# demo.cu build and environment setup (Ubuntu 24.04)

> Chinese version: [README.zh-CN.md](README.zh-CN.md)

This directory provides a minimal CUDA C example (`demo.cu`) and explains:

- How to set up clang 18.1.3 and CUDA Toolkit 12.x on Ubuntu 24.04
- How to compile `demo.cu` into an executable and a PTX file
- How to run PTX 6.4 / sm_70 on ProtoGPU and validate the simulation
- Common issues and troubleshooting tips
- Related build docs and official reference links

Intended for developers who want to use a clang+CUDA toolchain with ProtoGPU for PTX simulation.

## 1. Environment preparation

> Note: clang and CUDA Toolkit can be installed in many ways (apt, conda, runfile, tarball, source build, etc.). The steps below show one common approach; adjust paths/versions for your machine.

### 1.1 Install clang 18.1.3

(Example: apt / LLVM official script)

```bash
# Add LLVM apt repo (if not already added)
wget https://apt.llvm.org/llvm.sh
chmod +x llvm.sh
sudo ./llvm.sh 18

# Check version
clang++ --version
# should print 18.1.3
```

### 1.2 Install CUDA Toolkit 12.x

(Example: NVIDIA runfile offline installer)

1. Download the runfile installer from the [NVIDIA CUDA Toolkit Archive](https://developer.nvidia.com/cuda-toolkit-archive)
2. Install:

```bash
sudo sh cuda_12.3.0_*.run
# Install Toolkit only, do not install the driver
# Follow prompts to set env vars
export PATH=/usr/local/cuda/bin:$PATH
export LD_LIBRARY_PATH=/usr/local/cuda/lib64:$LD_LIBRARY_PATH
```

3. Verify headers and libraries:

```bash
ls /usr/local/cuda/include/cuda_runtime.h
ls /usr/local/cuda/lib64/libcudart.so
```

## 2. Build demo.cu

### 2.1 Build the executable

```bash
clang++ demo.cu -o demo \
  --cuda-path=/usr/local/cuda \
  --cuda-gpu-arch=sm_70 \
  -L/usr/local/cuda/lib64 -lcudart \
  -I/usr/local/cuda/include
```

- `--cuda-path` specifies the CUDA Toolkit path (e.g. `/usr/local/cuda`; this is often a symlink to a specific CUDA version)
- `--cuda-gpu-arch` sets the target GPU architecture (e.g. `sm_70`)
- `-lcudart` links the CUDA runtime

### 2.2 Generate a PTX file

```bash
clang++ demo.cu -o demo.ptx \
  --cuda-path=/usr/local/cuda \
  --cuda-gpu-arch=sm_70 \
  --cuda-device-only \
  --cuda-feature=+ptx64 \
  -I/usr/local/cuda/include
```

- `--cuda-device-only` emits device code (PTX) only
- `--cuda-feature=+ptx64` enables 64-bit PTX (typically outputs `.address_size 64`)
- Another option is `--cuda-ptx-version`, but clang 18.1.3 **does not support** it

You can verify the PTX header for version and target:

```bash
head -n 20 demo.ptx
```

You should typically see:

- `.version 6.4`
- `.target sm_70`
- `.address_size 64`

---

## 2.3 Build streaming_demo.cu (streams + memcpyAsync + kernel)

This repo also provides a more “streaming-style” CUDA C demo:

- `cuda/demo/streaming_demo.cu`

It creates two streams, and on each stream performs H2D → kernel → D2H, then validates results and prints `OK`.

### 2.3.1 Build the executable

```bash
clang++ streaming_demo.cu -o streaming_demo \
  --cuda-path=/usr/local/cuda \
  --cuda-gpu-arch=sm_70 \
  -L/usr/local/cuda/lib64 -lcudart \
  -I/usr/local/cuda/include
```

### 2.3.2 Generate a PTX file (for shim PTX override)

```bash
clang++ streaming_demo.cu -S -o streaming_demo.ptx \
  --cuda-path=/usr/local/cuda \
  --cuda-gpu-arch=sm_70 \
  --cuda-device-only \
  --cuda-feature=+ptx64 \
  -I/usr/local/cuda/include
```

Notes:

- The current CUDA Runtime shim’s fatbin→PTX extraction is still MVP-level (different toolchains may embed PTX in tokenized/encoded form inside the fatbin), so it’s recommended to explicitly provide text PTX via `GPUSIM_CUDART_SHIM_PTX_OVERRIDE`.
- On Linux/WSL, `GPUSIM_CUDART_SHIM_PTX_OVERRIDE` may be a single PTX path or a `:`-delimited PTX path list. The shim searches listed PTX files in order, and the first PTX containing the requested `.entry` wins.
- `-S` forces emission of **text PTX assembly**; otherwise, some clang versions/flag combinations may treat the output as an object file or another intermediate, leading to a `*.ptx` that isn’t readable PTX text.

### 2.3.3 Run via the CUDA Runtime shim (Linux/WSL)

From the repo root, build the shim (producing `build/libcudart.so.12`), put it on the loader’s search path, and set the PTX override:

```bash
# repo root
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build

export LD_LIBRARY_PATH="$PWD/build:${LD_LIBRARY_PATH}"
export GPUSIM_CUDART_SHIM_PTX_OVERRIDE="$PWD/cuda/demo/streaming_demo.ptx"

./cuda/demo/streaming_demo
```

Multi-PTX example on Linux/WSL:

```bash
export GPUSIM_CUDART_SHIM_PTX_OVERRIDE="$PWD/cuda/demo/streaming_demo_a.ptx:$PWD/cuda/demo/streaming_demo_b.ptx"
```

If the override env var is explicitly set and any listed PTX file is missing, empty, or not valid PTX text, the shim fails fast and does not fall back to fatbin extraction.

---

## 3. Run PTX 6.4 / sm_70 on ProtoGPU

Assuming you’ve built `demo.ptx` and prepared the corresponding asset files:

> Note: how to build `build/gpu-sim-cli` is documented in [../../docs/doc_build/build.md](../../docs/doc_build/build.md).

It’s recommended to run from the **repo root** (paths below match this assumption):

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

- `--entry` sets the kernel name (keep it consistent with `demo.cu`)
- `--grid` / `--block` correspond to kernel launch parameters (match the host code)

If you see errors like `entry not found`, open the PTX and find the actual `.entry ...` name (it may be C++ name-mangled, e.g. `_Z10add_kernelPKjS0_Pjj`), and use that value for `--entry`.

### 3.1 About `.param` (ld.param) and argument injection

The kernel entry in `demo.ptx` takes parameters (3 pointers + 1 u32). If you run it directly in “single kernel mode” as above, the current implementation **does not** automatically inject the parameter blob, so it will fail at the first `ld.param.*` (e.g. `E_PARAM_MISS`).

To make `add_kernel` actually execute, use **workload mode** to allocate device buffers, perform H2D/D2H copies, and pack kernel parameters:

```bash
./build/gpu-sim-cli \
  --config assets/configs/demo_config.json \
  --workload cuda/demo/add_kernel_workload.json \
  --trace out/demo.trace.jsonl \
  --stats out/demo.stats.json
```

Notes:

- The workload file allocates 3 device buffers (A/B/C, each 256 u32), and maps parameter names (e.g. `_Z10add_kernelPKjS0_Pjj_param_0`) to those buffers’ device pointers.
- In this workload, A/B are initialized to zeros, so C is expected to be zeros. Today this primarily validates “parameter passing + global memory + control flow can run end-to-end”.

If the simulation finishes without errors, your PTX and asset configuration is correct.

---

## 3.2 warp_reduce_add demo (split executable/PTX sources)

For custom PTX opcodes (for example `warp_reduce_add`), use split sources:

- Host executable source: `cuda/demo/warp_reduce_add_demo_executable.cu`
- PTX override source: `cuda/demo/warp_reduce_add_demo_ptx.cu`

Build host executable:

```bash
clang++ warp_reduce_add_demo_executable.cu -o warp_reduce_add_demo_executable \
  --cuda-path=/usr/local/cuda \
  --cuda-gpu-arch=sm_70 \
  -L/usr/local/cuda/lib64 -lcudart \
  -I/usr/local/cuda/include
```

Generate PTX override text:

```bash
clang++ warp_reduce_add_demo_ptx.cu -S -o warp_reduce_add_demo.ptx \
  --cuda-path=/usr/local/cuda \
  --cuda-gpu-arch=sm_70 \
  --cuda-device-only \
  --cuda-feature=+ptx64 \
  -I/usr/local/cuda/include
```

Run through shim:

```bash
export LD_LIBRARY_PATH="$PWD/build:${LD_LIBRARY_PATH}"
export GPUSIM_CUDART_SHIM_PTX_OVERRIDE="$PWD/cuda/demo/warp_reduce_add_demo.ptx"
./cuda/demo/warp_reduce_add_demo_executable
```

Why split this way: a normal host CUDA build path invokes `ptxas`, which rejects unknown custom opcodes. Using a separate device-only PTX override keeps the host binary toolchain-compatible while still validating custom opcode execution in ProtoGPU.

---

## 4. Common issues

- clang 18.x only supports CUDA Toolkit ≤ 12.3; CUDA 13.x+ headers are incompatible.
- If headers cannot be found (e.g. `texture_fetch_functions.h`), verify your CUDA Toolkit path/version (e.g. `/usr/local/cuda`).
- It’s recommended to use a consistent clang + CUDA version pair and avoid mixing with system nvcc/clang.
- If ProtoGPU reports missing assets or unsupported instructions, add/complete the corresponding JSON files.
- Some clang 18 builds do not support `--cuda-ptx-version`; if you get an error, remove that flag.

---

## 5. References

- [LLVM releases](https://releases.llvm.org/)
- [NVIDIA CUDA Toolkit Archive](https://developer.nvidia.com/cuda-toolkit-archive)
- [Clang CUDA support](https://clang.llvm.org/docs/UsersManual.html#cuda)
