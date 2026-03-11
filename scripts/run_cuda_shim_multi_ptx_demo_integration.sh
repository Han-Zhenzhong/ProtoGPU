#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

BUILD_DIR="${1:-build}"
CONFIG="${CONFIG:-Release}"

SKIP_RC="${GPUSIM_TEST_SKIP_RC:-0}"

cd "$REPO_ROOT"

if [[ "$(uname -s)" != "Linux"* ]]; then
  echo "[cudart-shim-multi-ptx-demo] skip: requires Linux/WSL (ELF demo)"
  exit "$SKIP_RC"
fi

DEMO="$REPO_ROOT/cuda/demo/demo"
REAL_PTX="$REPO_ROOT/cuda/demo/demo.ptx"

if [[ ! -f "$DEMO" ]]; then
  echo "[cudart-shim-multi-ptx-demo] skip: missing $DEMO"
  exit "$SKIP_RC"
fi
if [[ ! -x "$DEMO" ]]; then
  echo "[cudart-shim-multi-ptx-demo] skip: $DEMO is not executable (check git file mode)"
  exit "$SKIP_RC"
fi
if [[ ! -f "$REAL_PTX" ]]; then
  echo "[cudart-shim-multi-ptx-demo] skip: missing $REAL_PTX"
  exit "$SKIP_RC"
fi

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
  echo "[cudart-shim-multi-ptx-demo] build artifacts missing; building first"
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

DUMMY_PTX="$OUT_DIR/multi_ptx_dummy.ptx"
cat >"$DUMMY_PTX" <<'EOF'
.version 6.4
.target sm_70
.address_size 64

.visible .entry unused_kernel()
{
  ret;
}
EOF

GOOD_OVERRIDE="$DUMMY_PTX:$REAL_PTX"

GOOD_STDOUT="$OUT_DIR/cudart_multi_ptx_demo_stdout.txt"
GOOD_STDERR="$OUT_DIR/cudart_multi_ptx_demo_stderr.txt"

echo "[cudart-shim-multi-ptx-demo] running demo with ordered multi-PTX override"
set +e
LD_LIBRARY_PATH="$SHIM_DIR" \
  GPUSIM_CUDART_SHIM_PTX_OVERRIDE="$GOOD_OVERRIDE" \
  GPUSIM_CUDART_SHIM_LOG="${GPUSIM_CUDART_SHIM_LOG:-0}" \
  "$DEMO" >"$GOOD_STDOUT" 2>"$GOOD_STDERR"
RC=$?
set -e

if [[ $RC -ne 0 ]]; then
  echo "error: multi-PTX demo failed (exit=$RC)" >&2
  echo "--- stdout ($GOOD_STDOUT) ---" >&2
  tail -n 200 "$GOOD_STDOUT" >&2 || true
  echo "--- stderr ($GOOD_STDERR) ---" >&2
  tail -n 200 "$GOOD_STDERR" >&2 || true
  exit $RC
fi

if ! grep -Eq "^OK$|\bOK\b" "$GOOD_STDOUT"; then
  echo "error: multi-PTX demo did not print OK" >&2
  echo "--- stdout ($GOOD_STDOUT) ---" >&2
  tail -n 200 "$GOOD_STDOUT" >&2 || true
  echo "--- stderr ($GOOD_STDERR) ---" >&2
  tail -n 200 "$GOOD_STDERR" >&2 || true
  exit 1
fi

BAD_OVERRIDE="$DUMMY_PTX::${REAL_PTX}"
BAD_STDOUT="$OUT_DIR/cudart_multi_ptx_bad_stdout.txt"
BAD_STDERR="$OUT_DIR/cudart_multi_ptx_bad_stderr.txt"

echo "[cudart-shim-multi-ptx-demo] running malformed multi-PTX override scenario (expect failure)"
set +e
LD_LIBRARY_PATH="$SHIM_DIR" \
  GPUSIM_CUDART_SHIM_PTX_OVERRIDE="$BAD_OVERRIDE" \
  GPUSIM_CUDART_SHIM_LOG="${GPUSIM_CUDART_SHIM_LOG:-0}" \
  "$DEMO" >"$BAD_STDOUT" 2>"$BAD_STDERR"
RC=$?
set -e

if [[ $RC -eq 0 ]]; then
  echo "error: malformed multi-PTX override unexpectedly succeeded" >&2
  echo "--- stdout ($BAD_STDOUT) ---" >&2
  tail -n 200 "$BAD_STDOUT" >&2 || true
  echo "--- stderr ($BAD_STDERR) ---" >&2
  tail -n 200 "$BAD_STDERR" >&2 || true
  exit 1
fi

if ! grep -Fq "GPUSIM_CUDART_SHIM_PTX_OVERRIDE invalid" "$BAD_STDERR"; then
  echo "error: expected malformed override diagnostic in stderr" >&2
  echo "--- stderr ($BAD_STDERR) ---" >&2
  tail -n 200 "$BAD_STDERR" >&2 || true
  exit 1
fi

if ! grep -Fq "empty path element" "$BAD_STDERR"; then
  echo "error: expected empty path element diagnostic in stderr" >&2
  echo "--- stderr ($BAD_STDERR) ---" >&2
  tail -n 200 "$BAD_STDERR" >&2 || true
  exit 1
fi

echo "OK"