#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

BUILD_DIR="${1:-build}"
CONFIG="${CONFIG:-Release}"

cd "$REPO_ROOT"

need_build=0
if [[ ! -d "$BUILD_DIR" ]]; then
  need_build=1
fi

# If the CLI binary is missing, we also need a build.
if [[ $need_build -eq 0 ]]; then
  if [[ ! -f "$BUILD_DIR/gpu-sim-cli" && ! -f "$BUILD_DIR/gpu-sim-cli.exe" && ! -f "$BUILD_DIR/$CONFIG/gpu-sim-cli" && ! -f "$BUILD_DIR/$CONFIG/gpu-sim-cli.exe" ]]; then
    need_build=1
  fi
fi

if [[ $need_build -eq 1 ]]; then
  echo "[integration] build artifacts missing; building first"
  BUILD_TESTING=ON bash "$SCRIPT_DIR/build.sh" "$BUILD_DIR" "$CONFIG"
fi

CLI_CANDIDATES=(
  "$BUILD_DIR/gpu-sim-cli"
  "$BUILD_DIR/gpu-sim-cli.exe"
  "$BUILD_DIR/$CONFIG/gpu-sim-cli"
  "$BUILD_DIR/$CONFIG/gpu-sim-cli.exe"
)

CLI=""
for exe in "${CLI_CANDIDATES[@]}"; do
  if [[ -f "$exe" ]]; then
    CLI="$exe"
    break
  fi
done

if [[ -z "$CLI" ]]; then
  echo "error: cannot find gpu-sim-cli under $BUILD_DIR (config=$CONFIG)" >&2
  exit 2
fi

OUT_DIR="$BUILD_DIR/test_out"
rm -rf "$OUT_DIR"
mkdir -p "$OUT_DIR"

PTX="assets/ptx/demo_kernel.ptx"
PTX_ISA="assets/ptx_isa/demo_ptx8.json"
INST_DESC="assets/inst_desc/demo_desc.json"
CONFIG_JSON="assets/configs/demo_config.json"

echo "[integration] smoke run: demo_kernel.ptx"
set +e
"$CLI" \
  --ptx "$PTX" \
  --ptx-isa "$PTX_ISA" \
  --inst-desc "$INST_DESC" \
  --config "$CONFIG_JSON" \
  --trace "$OUT_DIR/trace.jsonl" \
  --stats "$OUT_DIR/stats.json" \
  >"$OUT_DIR/stdout.txt" 2>"$OUT_DIR/stderr.txt"
RC=$?
set -e

if [[ $RC -ne 0 ]]; then
  echo "error: smoke run failed (exit=$RC)" >&2
  echo "--- stdout ($OUT_DIR/stdout.txt) ---" >&2
  tail -n 200 "$OUT_DIR/stdout.txt" >&2 || true
  echo "--- stderr ($OUT_DIR/stderr.txt) ---" >&2
  tail -n 200 "$OUT_DIR/stderr.txt" >&2 || true
  exit $RC
fi

if [[ ! -s "$OUT_DIR/trace.jsonl" ]]; then
  echo "error: missing/empty trace: $OUT_DIR/trace.jsonl" >&2
  exit 1
fi
if [[ ! -s "$OUT_DIR/stats.json" ]]; then
  echo "error: missing/empty stats: $OUT_DIR/stats.json" >&2
  exit 1
fi

echo "[integration] io-demo: write_out.ptx"
set +e
"$CLI" \
  --ptx "assets/ptx/write_out.ptx" \
  --ptx-isa "$PTX_ISA" \
  --inst-desc "$INST_DESC" \
  --config "$CONFIG_JSON" \
  --trace "$OUT_DIR/io_trace.jsonl" \
  --stats "$OUT_DIR/io_stats.json" \
  --io-demo \
  >"$OUT_DIR/io_stdout.txt" 2>"$OUT_DIR/io_stderr.txt"
RC=$?
set -e

if [[ $RC -ne 0 ]]; then
  echo "error: io-demo run failed (exit=$RC)" >&2
  echo "--- stdout ($OUT_DIR/io_stdout.txt) ---" >&2
  tail -n 200 "$OUT_DIR/io_stdout.txt" >&2 || true
  echo "--- stderr ($OUT_DIR/io_stderr.txt) ---" >&2
  tail -n 200 "$OUT_DIR/io_stderr.txt" >&2 || true
  exit $RC
fi

if ! grep -Fq "io-demo u32 result: 42" "$OUT_DIR/io_stdout.txt"; then
  echo "error: expected io-demo output not found" >&2
  echo "--- stdout ($OUT_DIR/io_stdout.txt) ---" >&2
  tail -n 200 "$OUT_DIR/io_stdout.txt" >&2 || true
  exit 1
fi

echo "OK"
