#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

BUILD_DIR="${1:-build}"
CONFIG="${CONFIG:-Release}"

SKIP_RC="${GPUSIM_TEST_SKIP_RC:-0}"

cd "$REPO_ROOT"

if [[ "$(uname -s)" != "Linux"* ]]; then
  echo "[cudart-shim-demo] skip: requires Linux/WSL (ELF demo)"
  exit "$SKIP_RC"
fi

DEMO="$REPO_ROOT/cuda/demo/demo"
if [[ ! -f "$DEMO" ]]; then
  echo "[cudart-shim-demo] skip: missing $DEMO"
  exit "$SKIP_RC"
fi
if [[ ! -x "$DEMO" ]]; then
  echo "[cudart-shim-demo] skip: $DEMO is not executable (check git file mode)"
  exit "$SKIP_RC"
fi

# The shim's fatbin->PTX extraction is still MVP-level, so default to providing
# an explicit text PTX module for this demo.
DEFAULT_PTX_OVERRIDE="$REPO_ROOT/cuda/demo/demo.ptx"
PTX_OVERRIDE="${GPUSIM_CUDART_SHIM_PTX_OVERRIDE:-$DEFAULT_PTX_OVERRIDE}"
if [[ ! -f "$PTX_OVERRIDE" ]]; then
  if [[ -n "${GPUSIM_CUDART_SHIM_PTX_OVERRIDE:-}" ]]; then
    echo "error: GPUSIM_CUDART_SHIM_PTX_OVERRIDE points to missing file: $PTX_OVERRIDE" >&2
    exit 2
  fi
  echo "[cudart-shim-demo] skip: missing $DEFAULT_PTX_OVERRIDE" >&2
  exit "$SKIP_RC"
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
  BUILD_TESTING=ON bash "$SCRIPT_DIR/build.sh" "$BUILD_DIR" "$CONFIG"
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

echo "[cudart-shim-demo] running demo with LD_LIBRARY_PATH=$SHIM_DIR"
set +e
LD_LIBRARY_PATH="$SHIM_DIR" \
  GPUSIM_CUDART_SHIM_PTX_OVERRIDE="$PTX_OVERRIDE" \
  GPUSIM_CUDART_SHIM_LOG="${GPUSIM_CUDART_SHIM_LOG:-0}" \
  "$DEMO" >"$OUT_DIR/cudart_demo_stdout.txt" 2>"$OUT_DIR/cudart_demo_stderr.txt"
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
  if ! LD_LIBRARY_PATH="$SHIM_DIR" ldd "$DEMO" | grep -E "libcudart\\.so\\.12" | grep -Fq "$SHIM_DIR"; then
    echo "warning: ldd did not show libcudart.so.12 resolved from $SHIM_DIR (may still be OK with DT_RUNPATH/loader behavior)" >&2
  fi
fi

echo "OK"
