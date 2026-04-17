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
  BUILD_TESTING=ON bash "$SCRIPT_DIR/build_all.sh" "$BUILD_DIR" "$CONFIG"
fi

section() {
  echo
  echo "[integration] ==== $1 ===="
}

if command -v ctest >/dev/null 2>&1; then
  section "GPU Sim Tier-0 Regression"
  echo "[integration] running tiny GPT-2 minimal coverage via ctest (build dir: $BUILD_DIR, config: $CONFIG)"
  ctest --test-dir "$BUILD_DIR" -C "$CONFIG" -V -R "^gpu-sim-tiny-gpt2-mincov-tests$"
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

section "CUDA Shim E2E Integration Tests"

if [[ -f "$SCRIPT_DIR/run_cuda_shim_e2e_demo_integration.sh" ]]; then
  echo "[integration] running CUDA Runtime shim demo integration (if supported)"
  bash "$SCRIPT_DIR/run_cuda_shim_e2e_demo_integration.sh" "$BUILD_DIR"
fi

if [[ -f "$SCRIPT_DIR/run_cuda_shim_e2e_multi_ptx_demo_integration.sh" ]]; then
  echo "[integration] running CUDA Runtime shim multi-PTX demo integration (if supported)"
  bash "$SCRIPT_DIR/run_cuda_shim_e2e_multi_ptx_demo_integration.sh" "$BUILD_DIR"
fi

if [[ -f "$SCRIPT_DIR/run_cuda_shim_e2e_streaming_demo_cu.sh" ]]; then
  echo "[integration] running CUDA Runtime shim streaming .cu demo (if supported)"
  bash "$SCRIPT_DIR/run_cuda_shim_e2e_streaming_demo_cu.sh" "$BUILD_DIR"
fi

if [[ -f "$SCRIPT_DIR/run_cuda_shim_e2e_warp_reduce_add_demo_cu.sh" ]]; then
  echo "[integration] running CUDA Runtime shim warp_reduce_add .cu demo (if supported)"
  bash "$SCRIPT_DIR/run_cuda_shim_e2e_warp_reduce_add_demo_cu.sh" "$BUILD_DIR"
fi

if [[ -f "$SCRIPT_DIR/run_cuda_shim_e2e_warp_reduce_add_demo_alternative_cu.sh" ]]; then
  echo "[integration] running CUDA Runtime shim warp_reduce_add alternative .cu demo (if supported)"
  bash "$SCRIPT_DIR/run_cuda_shim_e2e_warp_reduce_add_demo_alternative_cu.sh" "$BUILD_DIR"
fi

PTX="assets/ptx/demo_kernel.ptx"
PTX_DIVERGE="assets/ptx/demo_divergence.ptx"
PTX_ISA="assets/ptx_isa/demo_ptx64.json"
INST_DESC="assets/inst_desc/demo_desc.json"
CONFIG_JSON="assets/configs/demo_config.json"
PAR_CONFIG_JSON="assets/configs/demo_parallel_config.json"
MODSEL_CONFIG_JSON="assets/configs/demo_modular_selectors.json"

section "GPU Sim CLI Integration Tests"

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

if ! grep -Fq '"action":"TRACE_HEADER"' "$OUT_DIR/trace.jsonl"; then
  echo "error: expected TRACE_HEADER as trace JSONL metadata" >&2
  echo "--- head ($OUT_DIR/trace.jsonl) ---" >&2
  head -n 5 "$OUT_DIR/trace.jsonl" >&2 || true
  exit 1
fi
if [[ ! -s "$OUT_DIR/stats.json" ]]; then
  echo "error: missing/empty stats: $OUT_DIR/stats.json" >&2
  exit 1
fi

if ! grep -Fq '"action":"RUN_START"' "$OUT_DIR/trace.jsonl"; then
  echo "error: expected RUN_START in trace (config_summary missing?)" >&2
  echo "--- tail ($OUT_DIR/trace.jsonl) ---" >&2
  tail -n 50 "$OUT_DIR/trace.jsonl" >&2 || true
  exit 1
fi

if ! grep -Fq "memory_model" "$OUT_DIR/trace.jsonl"; then
  echo "error: expected memory_model to be observable in RUN_START extra" >&2
  echo "--- tail ($OUT_DIR/trace.jsonl) ---" >&2
  tail -n 50 "$OUT_DIR/trace.jsonl" >&2 || true
  exit 1
fi

echo "[integration] divergence smoke: demo_divergence.ptx (expect SIMT_SPLIT)"
set +e
"$CLI" \
  --ptx "$PTX_DIVERGE" \
  --ptx-isa "$PTX_ISA" \
  --inst-desc "$INST_DESC" \
  --config "$CONFIG_JSON" \
  --block 2,1,1 \
  --trace "$OUT_DIR/trace_diverge.jsonl" \
  --stats "$OUT_DIR/stats_diverge.json" \
  >"$OUT_DIR/stdout_diverge.txt" 2>"$OUT_DIR/stderr_diverge.txt"
RC=$?
set -e

if [[ $RC -ne 0 ]]; then
  echo "error: divergence smoke run failed (exit=$RC)" >&2
  echo "--- stdout ($OUT_DIR/stdout_diverge.txt) ---" >&2
  tail -n 200 "$OUT_DIR/stdout_diverge.txt" >&2 || true
  echo "--- stderr ($OUT_DIR/stderr_diverge.txt) ---" >&2
  tail -n 200 "$OUT_DIR/stderr_diverge.txt" >&2 || true
  exit $RC
fi

if [[ ! -s "$OUT_DIR/trace_diverge.jsonl" ]]; then
  echo "error: missing/empty trace: $OUT_DIR/trace_diverge.jsonl" >&2
  exit 1
fi

if ! grep -Fq '"action":"SIMT_SPLIT"' "$OUT_DIR/trace_diverge.jsonl"; then
  echo "error: expected SIMT_SPLIT in divergence trace" >&2
  echo "--- tail ($OUT_DIR/trace_diverge.jsonl) ---" >&2
  tail -n 100 "$OUT_DIR/trace_diverge.jsonl" >&2 || true
  exit 1
fi

echo "[integration] sm parallel smoke: demo_parallel_config.json"
set +e
"$CLI" \
  --ptx "$PTX" \
  --ptx-isa "$PTX_ISA" \
  --inst-desc "$INST_DESC" \
  --config "$PAR_CONFIG_JSON" \
  --grid 4,1,1 \
  --block 32,1,1 \
  --trace "$OUT_DIR/trace_parallel.jsonl" \
  --stats "$OUT_DIR/stats_parallel.json" \
  >"$OUT_DIR/stdout_parallel.txt" 2>"$OUT_DIR/stderr_parallel.txt"
RC=$?
set -e

if [[ $RC -ne 0 ]]; then
  echo "error: sm parallel smoke run failed (exit=$RC)" >&2
  echo "--- stdout ($OUT_DIR/stdout_parallel.txt) ---" >&2
  tail -n 200 "$OUT_DIR/stdout_parallel.txt" >&2 || true
  echo "--- stderr ($OUT_DIR/stderr_parallel.txt) ---" >&2
  tail -n 200 "$OUT_DIR/stderr_parallel.txt" >&2 || true
  exit $RC
fi

if [[ ! -s "$OUT_DIR/trace_parallel.jsonl" ]]; then
  echo "error: missing/empty trace: $OUT_DIR/trace_parallel.jsonl" >&2
  exit 1
fi

if ! grep -Fq '"sm_id":' "$OUT_DIR/trace_parallel.jsonl"; then
  echo "error: expected sm_id in parallel trace (did parallel mode run?)" >&2
  echo "--- tail ($OUT_DIR/trace_parallel.jsonl) ---" >&2
  tail -n 50 "$OUT_DIR/trace_parallel.jsonl" >&2 || true
  exit 1
fi

echo "[integration] modular selectors smoke: demo_modular_selectors.json"
set +e
"$CLI" \
  --ptx "$PTX" \
  --ptx-isa "$PTX_ISA" \
  --inst-desc "$INST_DESC" \
  --config "$MODSEL_CONFIG_JSON" \
  --grid 4,1,1 \
  --block 32,1,1 \
  --trace "$OUT_DIR/trace_modsel.jsonl" \
  --stats "$OUT_DIR/stats_modsel.json" \
  >"$OUT_DIR/stdout_modsel.txt" 2>"$OUT_DIR/stderr_modsel.txt"
RC=$?
set -e

if [[ $RC -ne 0 ]]; then
  echo "error: modular selectors smoke run failed (exit=$RC)" >&2
  echo "--- stdout ($OUT_DIR/stdout_modsel.txt) ---" >&2
  tail -n 200 "$OUT_DIR/stdout_modsel.txt" >&2 || true
  echo "--- stderr ($OUT_DIR/stderr_modsel.txt) ---" >&2
  tail -n 200 "$OUT_DIR/stderr_modsel.txt" >&2 || true
  exit $RC
fi

if [[ ! -s "$OUT_DIR/trace_modsel.jsonl" ]]; then
  echo "error: missing/empty trace: $OUT_DIR/trace_modsel.jsonl" >&2
  exit 1
fi
if ! grep -Fq '"action":"RUN_START"' "$OUT_DIR/trace_modsel.jsonl"; then
  echo "error: expected RUN_START in modular selectors trace" >&2
  exit 1
fi
if ! grep -Fq "sm_round_robin" "$OUT_DIR/trace_modsel.jsonl"; then
  echo "error: expected cta_scheduler=sm_round_robin to be observable" >&2
  exit 1
fi
if ! grep -Fq "round_robin_interleave_step" "$OUT_DIR/trace_modsel.jsonl"; then
  echo "error: expected warp_scheduler=round_robin_interleave_step to be observable" >&2
  exit 1
fi

section "GPU Sim Launch And IO Integration Tests"

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

section "GPU Sim Workload Integration Tests"

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
