#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

BUILD_DIR="${1:-build}"
CONFIG="${CONFIG:-Release}"

# Allow callers/CTest to treat missing toolchain as skip.
SKIP_RC="${GPUSIM_TEST_SKIP_RC:-0}"

CUDA_PATH="${CUDA_PATH:-${CUDA_HOME:-/usr/local/cuda}}"
CLANGXX="${CLANGXX:-clang++}"
ARCH="${GPUSIM_CUDA_ARCH:-sm_70}"

cd "$REPO_ROOT"

if [[ "$(uname -s)" != "Linux"* ]]; then
  echo "[cudart-shim-warp-reduce-add-cu] skip: requires Linux/WSL"
  exit "$SKIP_RC"
fi

if ! command -v "$CLANGXX" >/dev/null 2>&1; then
  echo "[cudart-shim-warp-reduce-add-cu] skip: $CLANGXX not found"
  exit "$SKIP_RC"
fi

if [[ ! -f "$CUDA_PATH/include/cuda_runtime.h" ]]; then
  echo "[cudart-shim-warp-reduce-add-cu] skip: CUDA headers not found under $CUDA_PATH/include"
  echo "[cudart-shim-warp-reduce-add-cu] hint: set CUDA_PATH=/path/to/cuda"
  exit "$SKIP_RC"
fi

if [[ ! -f "$CUDA_PATH/lib64/libcudart.so" && ! -f "$CUDA_PATH/lib64/libcudart.so.12" && ! -f "$CUDA_PATH/lib64/libcudart.so.11" ]]; then
  echo "[cudart-shim-warp-reduce-add-cu] skip: CUDA libcudart not found under $CUDA_PATH/lib64"
  echo "[cudart-shim-warp-reduce-add-cu] hint: set CUDA_PATH=/path/to/cuda"
  exit "$SKIP_RC"
fi

EXE_SRC="$REPO_ROOT/cuda/demo/warp_reduce_add_demo_executable.cu"
PTX_SRC="$REPO_ROOT/cuda/demo/warp_reduce_add_demo_ptx.cu"
if [[ ! -f "$EXE_SRC" ]]; then
  echo "[cudart-shim-warp-reduce-add-cu] skip: missing $EXE_SRC"
  exit "$SKIP_RC"
fi
if [[ ! -f "$PTX_SRC" ]]; then
  echo "[cudart-shim-warp-reduce-add-cu] skip: missing $PTX_SRC"
  exit "$SKIP_RC"
fi

# Ensure shim exists.
need_build=0
if [[ ! -d "$BUILD_DIR" ]]; then
  need_build=1
fi

SHIM_SO=""
for so in "$BUILD_DIR/libcudart.so.12" "$BUILD_DIR/$CONFIG/libcudart.so.12"; do
  if [[ -f "$so" ]]; then
    SHIM_SO="$so"
    break
  fi
done

if [[ $need_build -eq 0 && -z "$SHIM_SO" ]]; then
  need_build=1
fi

if [[ $need_build -eq 0 ]]; then
  ASSET_DEPENDS=(
    "$REPO_ROOT/assets/configs/demo_config.json"
    "$REPO_ROOT/assets/ptx_isa/demo_ptx64.json"
    "$REPO_ROOT/assets/inst_desc/demo_desc.json"
    "$REPO_ROOT/cmake/generate_cudart_shim_assets.cmake"
  )
  for dep in "${ASSET_DEPENDS[@]}"; do
    if [[ -f "$dep" && "$dep" -nt "$SHIM_SO" ]]; then
      need_build=1
      break
    fi
  done
fi

if [[ $need_build -eq 1 ]]; then
  echo "[cudart-shim-warp-reduce-add-cu] shim/assets out of date; building gpu-sim-cudart-shim"
  if [[ -f "$BUILD_DIR/CMakeCache.txt" ]]; then
    cmake --build "$BUILD_DIR" --config "$CONFIG" --target gpu-sim-cudart-shim
  else
    BUILD_TESTING=ON bash "$SCRIPT_DIR/build.sh" "$BUILD_DIR" "$CONFIG"
  fi
fi

SHIM_DIR=""
for d in "$BUILD_DIR" "$BUILD_DIR/$CONFIG"; do
  if [[ -f "$d/libcudart.so.12" ]]; then
    SHIM_DIR="$d"
    break
  fi
done

if [[ -z "$SHIM_DIR" ]]; then
  echo "error: cannot find libcudart.so.12 under $BUILD_DIR (config=$CONFIG)" >&2
  exit 2
fi

OUT_DIR="$BUILD_DIR/test_out"
mkdir -p "$OUT_DIR"

BIN="$OUT_DIR/warp_reduce_add_demo_executable"
PTX="$OUT_DIR/warp_reduce_add_demo.ptx"
PTX_OVERRIDE_VALUE="${GPUSIM_CUDART_SHIM_PTX_OVERRIDE:-$PTX}"
STDOUT="$OUT_DIR/cudart_warp_reduce_add_cu_stdout.txt"
STDERR="$OUT_DIR/cudart_warp_reduce_add_cu_stderr.txt"

echo "[cudart-shim-warp-reduce-add-cu] compiling host binary ($ARCH)"
set +e
"$CLANGXX" "$EXE_SRC" -o "$BIN" \
  --cuda-path="$CUDA_PATH" \
  --cuda-gpu-arch="$ARCH" \
  -I"$CUDA_PATH/include" -L"$CUDA_PATH/lib64" -lcudart \
  >"$OUT_DIR/cudart_warp_reduce_add_cu_build_stdout.txt" 2>"$OUT_DIR/cudart_warp_reduce_add_cu_build_stderr.txt"
RC=$?
set -e
if [[ $RC -ne 0 ]]; then
  echo "[cudart-shim-warp-reduce-add-cu] skip: failed to compile warp_reduce_add_demo_executable.cu (exit=$RC)" >&2
  echo "--- stderr ($OUT_DIR/cudart_warp_reduce_add_cu_build_stderr.txt) ---" >&2
  tail -n 200 "$OUT_DIR/cudart_warp_reduce_add_cu_build_stderr.txt" >&2 || true
  exit "$SKIP_RC"
fi

echo "[cudart-shim-warp-reduce-add-cu] generating text PTX for override"
set +e
"$CLANGXX" "$PTX_SRC" -S -o "$PTX" \
  --cuda-path="$CUDA_PATH" \
  --cuda-gpu-arch="$ARCH" \
  --cuda-device-only --cuda-feature=+ptx64 \
  -I"$CUDA_PATH/include" \
  >"$OUT_DIR/cudart_warp_reduce_add_cu_ptx_stdout.txt" 2>"$OUT_DIR/cudart_warp_reduce_add_cu_ptx_stderr.txt"
RC=$?
set -e
if [[ $RC -ne 0 ]]; then
  echo "[cudart-shim-warp-reduce-add-cu] skip: failed to generate PTX from warp_reduce_add_demo_ptx.cu (exit=$RC)" >&2
  echo "--- stderr ($OUT_DIR/cudart_warp_reduce_add_cu_ptx_stderr.txt) ---" >&2
  tail -n 200 "$OUT_DIR/cudart_warp_reduce_add_cu_ptx_stderr.txt" >&2 || true
  exit "$SKIP_RC"
fi

if [[ ! -s "$PTX" ]]; then
  echo "[cudart-shim-warp-reduce-add-cu] skip: PTX output missing/empty: $PTX" >&2
  exit "$SKIP_RC"
fi

# Ensure generated PTX actually contains the custom opcode.
if ! grep -Fq "warp_reduce_add" "$PTX"; then
  echo "error: generated PTX does not contain warp_reduce_add opcode" >&2
  echo "--- head ($PTX) ---" >&2
  head -n 120 "$PTX" >&2 || true
  exit 1
fi

echo "[cudart-shim-warp-reduce-add-cu] running under shim (LD_LIBRARY_PATH=$SHIM_DIR)"
set +e
LD_LIBRARY_PATH="$SHIM_DIR" \
  GPUSIM_CUDART_SHIM_PTX_OVERRIDE="$PTX_OVERRIDE_VALUE" \
  GPUSIM_CUDART_SHIM_LOG="${GPUSIM_CUDART_SHIM_LOG:-0}" \
  "$BIN" >"$STDOUT" 2>"$STDERR"
RC=$?
set -e

if [[ $RC -ne 0 ]]; then
  echo "error: warp_reduce_add_demo_executable failed (exit=$RC)" >&2
  echo "--- stdout ($STDOUT) ---" >&2
  tail -n 200 "$STDOUT" >&2 || true
  echo "--- stderr ($STDERR) ---" >&2
  tail -n 200 "$STDERR" >&2 || true
  exit $RC
fi

if ! grep -Eq "^OK$|\bOK\b" "$STDOUT"; then
  echo "error: warp_reduce_add_demo_executable did not print OK" >&2
  echo "--- stdout ($STDOUT) ---" >&2
  tail -n 200 "$STDOUT" >&2 || true
  echo "--- stderr ($STDERR) ---" >&2
  tail -n 200 "$STDERR" >&2 || true
  exit 1
fi

echo "OK"
