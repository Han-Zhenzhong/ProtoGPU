#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

BUILD_DIR="${1:-build}"
CONFIG="${CONFIG:-Release}"

# Allow callers/CTest to treat missing toolchain as a skip.
SKIP_RC="${GPUSIM_TEST_SKIP_RC:-0}"

CUDA_PATH="${CUDA_PATH:-${CUDA_HOME:-/usr/local/cuda}}"
CLANGXX="${CLANGXX:-clang++}"
ARCH="${GPUSIM_CUDA_ARCH:-sm_70}"

cd "$REPO_ROOT"

if [[ "$(uname -s)" != "Linux"* ]]; then
	echo "[cudart-shim-streaming-cu] skip: requires Linux/WSL"
	exit "$SKIP_RC"
fi

if ! command -v "$CLANGXX" >/dev/null 2>&1; then
	echo "[cudart-shim-streaming-cu] skip: $CLANGXX not found"
	exit "$SKIP_RC"
fi

if [[ ! -f "$CUDA_PATH/include/cuda_runtime.h" ]]; then
	echo "[cudart-shim-streaming-cu] skip: CUDA headers not found under $CUDA_PATH/include"
	echo "[cudart-shim-streaming-cu] hint: set CUDA_PATH=/path/to/cuda"
	exit "$SKIP_RC"
fi

if [[ ! -f "$CUDA_PATH/lib64/libcudart.so" && ! -f "$CUDA_PATH/lib64/libcudart.so.12" && ! -f "$CUDA_PATH/lib64/libcudart.so.11" ]]; then
	echo "[cudart-shim-streaming-cu] skip: CUDA libcudart not found under $CUDA_PATH/lib64"
	echo "[cudart-shim-streaming-cu] hint: set CUDA_PATH=/path/to/cuda"
	exit "$SKIP_RC"
fi

SRC="$REPO_ROOT/cuda/demo/streaming_demo.cu"
if [[ ! -f "$SRC" ]]; then
	echo "[cudart-shim-streaming-cu] skip: missing $SRC"
	exit "$SKIP_RC"
fi

# Ensure shim exists.
need_build=0
if [[ ! -d "$BUILD_DIR" ]]; then
	need_build=1
fi
if [[ $need_build -eq 0 ]]; then
	if [[ ! -f "$BUILD_DIR/libcudart.so.12" && ! -f "$BUILD_DIR/$CONFIG/libcudart.so.12" ]]; then
		need_build=1
	fi
fi

if [[ $need_build -eq 1 ]]; then
	echo "[cudart-shim-streaming-cu] build artifacts missing; building first"
	BUILD_TESTING=ON bash "$SCRIPT_DIR/build.sh" "$BUILD_DIR" "$CONFIG"
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

BIN="$OUT_DIR/streaming_demo"
PTX="$OUT_DIR/streaming_demo.ptx"
STDOUT="$OUT_DIR/cudart_streaming_cu_stdout.txt"
STDERR="$OUT_DIR/cudart_streaming_cu_stderr.txt"

echo "[cudart-shim-streaming-cu] compiling host binary ($ARCH)"
set +e
"$CLANGXX" "$SRC" -o "$BIN" \
	--cuda-path="$CUDA_PATH" \
	--cuda-gpu-arch="$ARCH" \
	-I"$CUDA_PATH/include" -L"$CUDA_PATH/lib64" -lcudart \
	>"$OUT_DIR/cudart_streaming_cu_build_stdout.txt" 2>"$OUT_DIR/cudart_streaming_cu_build_stderr.txt"
RC=$?
set -e
if [[ $RC -ne 0 ]]; then
	echo "[cudart-shim-streaming-cu] skip: failed to compile streaming_demo.cu (exit=$RC)" >&2
	echo "--- stdout ($OUT_DIR/cudart_streaming_cu_build_stdout.txt) ---" >&2
	tail -n 120 "$OUT_DIR/cudart_streaming_cu_build_stdout.txt" >&2 || true
	echo "--- stderr ($OUT_DIR/cudart_streaming_cu_build_stderr.txt) ---" >&2
	tail -n 200 "$OUT_DIR/cudart_streaming_cu_build_stderr.txt" >&2 || true
	exit "$SKIP_RC"
fi

echo "[cudart-shim-streaming-cu] generating text PTX for override"
set +e
"$CLANGXX" "$SRC" -S -o "$PTX" \
	--cuda-path="$CUDA_PATH" \
	--cuda-gpu-arch="$ARCH" \
	--cuda-device-only --cuda-feature=+ptx64 \
	-I"$CUDA_PATH/include" \
	>"$OUT_DIR/cudart_streaming_cu_ptx_stdout.txt" 2>"$OUT_DIR/cudart_streaming_cu_ptx_stderr.txt"
RC=$?
set -e
if [[ $RC -ne 0 ]]; then
	echo "[cudart-shim-streaming-cu] skip: failed to generate PTX (exit=$RC)" >&2
	echo "--- stderr ($OUT_DIR/cudart_streaming_cu_ptx_stderr.txt) ---" >&2
	tail -n 200 "$OUT_DIR/cudart_streaming_cu_ptx_stderr.txt" >&2 || true
	exit "$SKIP_RC"
fi

if [[ ! -s "$PTX" ]]; then
	echo "[cudart-shim-streaming-cu] skip: PTX output missing/empty: $PTX" >&2
	exit "$SKIP_RC"
fi

echo "[cudart-shim-streaming-cu] running under shim (LD_LIBRARY_PATH=$SHIM_DIR)"
set +e
LD_LIBRARY_PATH="$SHIM_DIR" \
	GPUSIM_CUDART_SHIM_PTX_OVERRIDE="$PTX" \
	GPUSIM_CUDART_SHIM_LOG="${GPUSIM_CUDART_SHIM_LOG:-0}" \
	"$BIN" >"$STDOUT" 2>"$STDERR"
RC=$?
set -e

if [[ $RC -ne 0 ]]; then
	echo "error: streaming_demo.cu failed (exit=$RC)" >&2
	echo "--- stdout ($STDOUT) ---" >&2
	tail -n 200 "$STDOUT" >&2 || true
	echo "--- stderr ($STDERR) ---" >&2
	tail -n 200 "$STDERR" >&2 || true
	exit $RC
fi

if ! grep -Eq "^OK$|\bOK\b" "$STDOUT"; then
	echo "error: streaming_demo.cu did not print OK" >&2
	echo "--- stdout ($STDOUT) ---" >&2
	tail -n 200 "$STDOUT" >&2 || true
	echo "--- stderr ($STDERR) ---" >&2
	tail -n 200 "$STDERR" >&2 || true
	exit 1
fi

echo "OK"