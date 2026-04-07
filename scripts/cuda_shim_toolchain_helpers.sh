#!/usr/bin/env bash

gpusim_cuda_toolchain_init() {
  CUDA_PATH="${CUDA_PATH:-${CUDA_HOME:-/usr/local/cuda}}"
  CLANGXX="${CLANGXX:-clang++}"
  ARCH="${GPUSIM_CUDA_ARCH:-sm_70}"
}

gpusim_cuda_toolchain_available() {
  command -v "$CLANGXX" >/dev/null 2>&1 || return 1

  [[ -f "$CUDA_PATH/include/cuda_runtime.h" ]] || return 1

  [[ -f "$CUDA_PATH/lib64/libcudart.so" || \
     -f "$CUDA_PATH/lib64/libcudart.so.12" || \
     -f "$CUDA_PATH/lib64/libcudart.so.11" ]] || return 1

  return 0
}

gpusim_cuda_compile_host_binary() {
  local label="$1"
  local src="$2"
  local out_bin="$3"
  local out_dir="$4"

  set +e
  "$CLANGXX" "$src" -o "$out_bin" \
    --cuda-path="$CUDA_PATH" \
    --cuda-gpu-arch="$ARCH" \
    -I"$CUDA_PATH/include" -L"$CUDA_PATH/lib64" -lcudart \
    >"$out_dir/${label}_build_stdout.txt" 2>"$out_dir/${label}_build_stderr.txt"
  local rc=$?
  set -e

  if [[ $rc -ne 0 ]]; then
    echo "error: failed to compile $(basename "$src") (exit=$rc)" >&2
    echo "--- stdout ($out_dir/${label}_build_stdout.txt) ---" >&2
    tail -n 120 "$out_dir/${label}_build_stdout.txt" >&2 || true
    echo "--- stderr ($out_dir/${label}_build_stderr.txt) ---" >&2
    tail -n 200 "$out_dir/${label}_build_stderr.txt" >&2 || true
  fi

  return $rc
}

gpusim_cuda_generate_ptx() {
  local label="$1"
  local src="$2"
  local out_ptx="$3"
  local out_dir="$4"

  set +e
  "$CLANGXX" "$src" -S -o "$out_ptx" \
    --cuda-path="$CUDA_PATH" \
    --cuda-gpu-arch="$ARCH" \
    --cuda-device-only --cuda-feature=+ptx64 \
    -I"$CUDA_PATH/include" \
    >"$out_dir/${label}_ptx_stdout.txt" 2>"$out_dir/${label}_ptx_stderr.txt"
  local rc=$?
  set -e

  if [[ $rc -ne 0 ]]; then
    echo "error: failed to generate PTX from $(basename "$src") (exit=$rc)" >&2
    echo "--- stderr ($out_dir/${label}_ptx_stderr.txt) ---" >&2
    tail -n 200 "$out_dir/${label}_ptx_stderr.txt" >&2 || true
    return $rc
  fi

  if [[ ! -s "$out_ptx" ]]; then
    echo "error: PTX output missing/empty: $out_ptx" >&2
    return 1
  fi

  return 0
}