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

echo "[integration] 3D launch smoke: --grid 2,2,1 --block 40,1,1"
set +e
"$CLI" \
  --ptx "$PTX" \
  --ptx-isa "$PTX_ISA" \
  --inst-desc "$INST_DESC" \
  --config "$CONFIG_JSON" \
  --grid 2,2,1 \
  --block 40,1,1 \
  --trace "$OUT_DIR/trace_3d.jsonl" \
  --stats "$OUT_DIR/stats_3d.json" \
  >"$OUT_DIR/stdout_3d.txt" 2>"$OUT_DIR/stderr_3d.txt"
RC=$?
set -e

if [[ $RC -ne 0 ]]; then
  echo "error: 3D launch run failed (exit=$RC)" >&2
  echo "--- stdout ($OUT_DIR/stdout_3d.txt) ---" >&2
  tail -n 200 "$OUT_DIR/stdout_3d.txt" >&2 || true
  echo "--- stderr ($OUT_DIR/stderr_3d.txt) ---" >&2
  tail -n 200 "$OUT_DIR/stderr_3d.txt" >&2 || true
  exit $RC
fi

if [[ ! -s "$OUT_DIR/trace_3d.jsonl" ]]; then
  echo "error: missing/empty trace: $OUT_DIR/trace_3d.jsonl" >&2
  exit 1
fi
if [[ ! -s "$OUT_DIR/stats_3d.json" ]]; then
  echo "error: missing/empty stats: $OUT_DIR/stats_3d.json" >&2
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

echo "[integration] workload smoke: single stream (H2D -> KERNEL -> D2H -> sync)"
set +e
"$CLI" \
  --config "$CONFIG_JSON" \
  --workload "assets/workloads/smoke_single_stream.json" \
  --trace "$OUT_DIR/workload_single.trace.jsonl" \
  --stats "$OUT_DIR/workload_single.stats.json" \
  >"$OUT_DIR/workload_single.stdout.txt" 2>"$OUT_DIR/workload_single.stderr.txt"
RC=$?
set -e

if [[ $RC -ne 0 ]]; then
  echo "error: workload single-stream run failed (exit=$RC)" >&2
  echo "--- stdout ($OUT_DIR/workload_single.stdout.txt) ---" >&2
  tail -n 200 "$OUT_DIR/workload_single.stdout.txt" >&2 || true
  echo "--- stderr ($OUT_DIR/workload_single.stderr.txt) ---" >&2
  tail -n 200 "$OUT_DIR/workload_single.stderr.txt" >&2 || true
  exit $RC
fi

if [[ ! -s "$OUT_DIR/workload_single.trace.jsonl" ]]; then
  echo "error: missing/empty trace: $OUT_DIR/workload_single.trace.jsonl" >&2
  exit 1
fi

if ! grep -Fq '"cat":"STREAM"' "$OUT_DIR/workload_single.trace.jsonl"; then
  echo "error: expected STREAM events in workload trace" >&2
  exit 1
fi
if ! grep -Fq '"action":"cmd_enq"' "$OUT_DIR/workload_single.trace.jsonl"; then
  echo "error: expected cmd_enq in workload trace" >&2
  exit 1
fi

echo "[integration] workload smoke: two streams (event_record/wait)"
set +e
"$CLI" \
  --config "$CONFIG_JSON" \
  --workload "assets/workloads/smoke_two_stream_event.json" \
  --trace "$OUT_DIR/workload_event.trace.jsonl" \
  --stats "$OUT_DIR/workload_event.stats.json" \
  >"$OUT_DIR/workload_event.stdout.txt" 2>"$OUT_DIR/workload_event.stderr.txt"
RC=$?
set -e

if [[ $RC -ne 0 ]]; then
  echo "error: workload two-stream-event run failed (exit=$RC)" >&2
  echo "--- stdout ($OUT_DIR/workload_event.stdout.txt) ---" >&2
  tail -n 200 "$OUT_DIR/workload_event.stdout.txt" >&2 || true
  echo "--- stderr ($OUT_DIR/workload_event.stderr.txt) ---" >&2
  tail -n 200 "$OUT_DIR/workload_event.stderr.txt" >&2 || true
  exit $RC
fi

if [[ ! -s "$OUT_DIR/workload_event.trace.jsonl" ]]; then
  echo "error: missing/empty trace: $OUT_DIR/workload_event.trace.jsonl" >&2
  exit 1
fi

if ! grep -Fq '"action":"event_record"' "$OUT_DIR/workload_event.trace.jsonl"; then
  echo "error: expected event_record in workload trace" >&2
  exit 1
fi
if ! grep -Fq '"action":"event_wait_done"' "$OUT_DIR/workload_event.trace.jsonl"; then
  echo "error: expected event_wait_done in workload trace" >&2
  exit 1
fi

echo "OK"
