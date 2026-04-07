#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

source "$SCRIPT_DIR/cuda_shim_toolchain_helpers.sh"

BUILD_DIR="${1:-build}"
CONFIG="${CONFIG:-Release}"

SKIP_RC="${GPUSIM_TEST_SKIP_RC:-0}"
TOOLCHAIN_OK=0

validate_ptx_override_value() {
  local value="$1"
  local part=""
  local -a parts=()

  IFS=':' read -r -a parts <<< "$value"
  for part in "${parts[@]}"; do
    part="${part#${part%%[![:space:]]*}}"
    part="${part%${part##*[![:space:]]}}"
    if [[ -z "$part" ]]; then
      echo "error: GPUSIM_CUDART_SHIM_PTX_OVERRIDE contains an empty path element: $value" >&2
      return 2
    fi
    if [[ ! -f "$part" ]]; then
      echo "error: GPUSIM_CUDART_SHIM_PTX_OVERRIDE points to missing file: $part" >&2
      return 2
    fi
  done
}

cd "$REPO_ROOT"

if [[ "$(uname -s)" != "Linux"* ]]; then
  echo "[cudart-shim-demo] skip: requires Linux/WSL (ELF demo)"
  exit "$SKIP_RC"
fi

gpusim_cuda_toolchain_init
if gpusim_cuda_toolchain_available; then
  TOOLCHAIN_OK=1
fi

SRC="$REPO_ROOT/cuda/demo/demo.cu"
PREBUILT_BIN="$REPO_ROOT/cuda/demo/demo"
PREBUILT_PTX="$REPO_ROOT/cuda/demo/demo.ptx"

if [[ ! -f "$SRC" && ! -f "$PREBUILT_BIN" ]]; then
  echo "[cudart-shim-demo] skip: missing both $SRC and $PREBUILT_BIN"
  exit "$SKIP_RC"
fi

# The shim's fatbin->PTX extraction is still MVP-level, so default to providing
# an explicit text PTX module for this demo.
PTX_OVERRIDE="${GPUSIM_CUDART_SHIM_PTX_OVERRIDE:-}"
if [[ -n "$PTX_OVERRIDE" ]]; then
  validate_ptx_override_value "$PTX_OVERRIDE" || exit $?
fi

need_build=0
if [[ ! -d "$BUILD_DIR" ]]; then
  need_build=1
fi

# If the shim library is missing, we also need a build.
if [[ $need_build -eq 0 ]]; then
  if [[ ! -f "$BUILD_DIR/libcudart.so.12" && ! -f "$BUILD_DIR/$CONFIG/libcudart.so.12" ]]; then
    need_build=1
  fi
fi

if [[ $need_build -eq 1 ]]; then
  echo "[cudart-shim-demo] build artifacts missing; building first"
  BUILD_TESTING=ON bash "$SCRIPT_DIR/build_all.sh" "$BUILD_DIR" "$CONFIG"
fi

SHIM_DIR=""
CANDIDATES=(
  "$BUILD_DIR"
  "$BUILD_DIR/$CONFIG"
)
for d in "${CANDIDATES[@]}"; do
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

BIN="$OUT_DIR/demo"
PTX="$OUT_DIR/demo.ptx"
BIN_TO_RUN="$BIN"
PTX_TO_USE="$PTX"

if [[ $TOOLCHAIN_OK -eq 1 && -f "$SRC" ]]; then
  echo "[cudart-shim-demo] CUDA toolkit detected; compiling demo.cu host binary ($ARCH)"
  gpusim_cuda_compile_host_binary "cudart_demo" "$SRC" "$BIN" "$OUT_DIR" || exit $?

  echo "[cudart-shim-demo] generating text PTX for override"
  gpusim_cuda_generate_ptx "cudart_demo" "$SRC" "$PTX" "$OUT_DIR" || exit $?
else
  echo "[cudart-shim-demo] CUDA toolkit unavailable; using prebuilt demo artifacts"
  BIN_TO_RUN="$PREBUILT_BIN"
  PTX_TO_USE="$PREBUILT_PTX"
  if [[ ! -f "$BIN_TO_RUN" ]]; then
    echo "[cudart-shim-demo] skip: missing prebuilt executable: $BIN_TO_RUN" >&2
    exit "$SKIP_RC"
  fi
  if [[ ! -x "$BIN_TO_RUN" ]]; then
    chmod +x "$BIN_TO_RUN" || true
  fi
  if [[ ! -x "$BIN_TO_RUN" ]]; then
    echo "[cudart-shim-demo] skip: prebuilt executable is not executable: $BIN_TO_RUN" >&2
    exit "$SKIP_RC"
  fi
  if [[ ! -s "$PTX_TO_USE" ]]; then
    echo "[cudart-shim-demo] skip: missing/empty prebuilt PTX: $PTX_TO_USE" >&2
    exit "$SKIP_RC"
  fi
fi

if [[ -z "$PTX_OVERRIDE" ]]; then
  PTX_OVERRIDE="$PTX_TO_USE"
fi

echo "[cudart-shim-demo] running demo with LD_LIBRARY_PATH=$SHIM_DIR"
set +e
LD_LIBRARY_PATH="$SHIM_DIR" \
  GPUSIM_CUDART_SHIM_PTX_OVERRIDE="$PTX_OVERRIDE" \
  GPUSIM_CUDART_SHIM_LOG="${GPUSIM_CUDART_SHIM_LOG:-0}" \
  "$BIN_TO_RUN" >"$OUT_DIR/cudart_demo_stdout.txt" 2>"$OUT_DIR/cudart_demo_stderr.txt"
RC=$?
set -e

if [[ $RC -ne 0 ]]; then
  echo "error: demo failed (exit=$RC)" >&2
  echo "--- stdout ($OUT_DIR/cudart_demo_stdout.txt) ---" >&2
  tail -n 200 "$OUT_DIR/cudart_demo_stdout.txt" >&2 || true
  echo "--- stderr ($OUT_DIR/cudart_demo_stderr.txt) ---" >&2
  tail -n 200 "$OUT_DIR/cudart_demo_stderr.txt" >&2 || true
  exit $RC
fi

if ! grep -Eq "^OK$|\bOK\b" "$OUT_DIR/cudart_demo_stdout.txt"; then
  echo "error: demo did not print OK" >&2
  echo "--- stdout ($OUT_DIR/cudart_demo_stdout.txt) ---" >&2
  tail -n 200 "$OUT_DIR/cudart_demo_stdout.txt" >&2 || true
  exit 1
fi

# Optional: verify the loader resolves libcudart from our build dir.
if command -v ldd >/dev/null 2>&1; then
  if ! LD_LIBRARY_PATH="$SHIM_DIR" ldd "$BIN_TO_RUN" | grep -E "libcudart\.so\.12" | grep -Fq "$SHIM_DIR"; then
    echo "warning: ldd did not show libcudart.so.12 resolved from $SHIM_DIR (may still be OK with DT_RUNPATH/loader behavior)" >&2
  fi
fi

echo "OK"
