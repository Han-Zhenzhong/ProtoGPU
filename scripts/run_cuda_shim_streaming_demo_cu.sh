#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

source "$SCRIPT_DIR/cuda_shim_toolchain_helpers.sh"

BUILD_DIR="${1:-build}"
CONFIG="${CONFIG:-Release}"

# Allow callers/CTest to treat missing toolchain as a skip.
SKIP_RC="${GPUSIM_TEST_SKIP_RC:-0}"
TOOLCHAIN_OK=0

cd "$REPO_ROOT"

if [[ "$(uname -s)" != "Linux"* ]]; then
	echo "[cudart-shim-streaming-cu] skip: requires Linux/WSL"
	exit "$SKIP_RC"
fi

gpusim_cuda_toolchain_init
if gpusim_cuda_toolchain_available; then
	TOOLCHAIN_OK=1
fi

SRC="$REPO_ROOT/cuda/demo/streaming_demo.cu"
PREBUILT_BIN="$REPO_ROOT/cuda/demo/streaming_demo"
PREBUILT_PTX="$REPO_ROOT/cuda/demo/streaming_demo.ptx"
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
	BUILD_TESTING=ON bash "$SCRIPT_DIR/build_all.sh" "$BUILD_DIR" "$CONFIG"
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
BIN_TO_RUN="$BIN"
PTX_TO_USE="$PTX"
STDOUT="$OUT_DIR/cudart_streaming_cu_stdout.txt"
STDERR="$OUT_DIR/cudart_streaming_cu_stderr.txt"

if [[ $TOOLCHAIN_OK -eq 1 ]]; then
	echo "[cudart-shim-streaming-cu] CUDA toolkit detected; compiling host binary ($ARCH)"
	gpusim_cuda_compile_host_binary "cudart_streaming_cu" "$SRC" "$BIN" "$OUT_DIR" || exit $?

	echo "[cudart-shim-streaming-cu] generating text PTX for override"
	gpusim_cuda_generate_ptx "cudart_streaming_cu" "$SRC" "$PTX" "$OUT_DIR" || exit $?
else
	echo "[cudart-shim-streaming-cu] CUDA toolkit unavailable; using prebuilt demo artifacts"
	BIN_TO_RUN="$PREBUILT_BIN"
	PTX_TO_USE="$PREBUILT_PTX"
	if [[ ! -f "$BIN_TO_RUN" ]]; then
		echo "error: missing prebuilt executable: $BIN_TO_RUN" >&2
		exit 2
	fi
	if [[ ! -x "$BIN_TO_RUN" ]]; then
		chmod +x "$BIN_TO_RUN" || true
	fi
	if [[ ! -x "$BIN_TO_RUN" ]]; then
		echo "error: prebuilt executable is not executable: $BIN_TO_RUN" >&2
		exit 2
	fi
	if [[ ! -s "$PTX_TO_USE" ]]; then
		echo "error: missing/empty prebuilt PTX: $PTX_TO_USE" >&2
		exit 2
	fi
fi

PTX_OVERRIDE_VALUE="${GPUSIM_CUDART_SHIM_PTX_OVERRIDE:-$PTX_TO_USE}"

echo "[cudart-shim-streaming-cu] running under shim (LD_LIBRARY_PATH=$SHIM_DIR)"
set +e
LD_LIBRARY_PATH="$SHIM_DIR" \
	GPUSIM_CUDART_SHIM_PTX_OVERRIDE="$PTX_OVERRIDE_VALUE" \
	GPUSIM_CUDART_SHIM_LOG="${GPUSIM_CUDART_SHIM_LOG:-0}" \
	"$BIN_TO_RUN" >"$STDOUT" 2>"$STDERR"
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