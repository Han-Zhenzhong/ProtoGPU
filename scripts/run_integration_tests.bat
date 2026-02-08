@echo off
setlocal enabledelayedexpansion

set SCRIPT_DIR=%~dp0
pushd "%SCRIPT_DIR%\.." >nul

set BUILD_DIR=%~1
if "%BUILD_DIR%"=="" set BUILD_DIR=build

if "%CONFIG%"=="" (
  set CONFIG=Release
)

set NEED_BUILD=0
if not exist "%BUILD_DIR%" set NEED_BUILD=1

if %NEED_BUILD%==0 (
  if not exist "%BUILD_DIR%\gpu-sim-cli.exe" if not exist "%BUILD_DIR%\%CONFIG%\gpu-sim-cli.exe" set NEED_BUILD=1
)

if %NEED_BUILD%==1 (
  echo [integration] build artifacts missing; building first
  set BUILD_TESTING=ON
  call "%SCRIPT_DIR%\build.bat" "%BUILD_DIR%" "%CONFIG%"
  if errorlevel 1 (
    popd >nul
    exit /b %errorlevel%
  )
)

where ctest >nul 2>nul
if %errorlevel%==0 (
  echo [integration] running tiny GPT-2 minimal coverage via ctest (build dir: %BUILD_DIR%, config: %CONFIG%)
  ctest --test-dir "%BUILD_DIR%" -C %CONFIG% -V -R "^gpu-sim-tiny-gpt2-mincov-tests$"
  if errorlevel 1 (
    popd >nul
    exit /b %errorlevel%
  )
)

set CLI=
for %%P in (
  "%BUILD_DIR%\gpu-sim-cli.exe"
  "%BUILD_DIR%\%CONFIG%\gpu-sim-cli.exe"
) do (
  if exist %%~P set CLI=%%~P
)

if "%CLI%"=="" (
  echo error: cannot find gpu-sim-cli.exe under %BUILD_DIR% (config=%CONFIG%)
  popd >nul
  exit /b 2
)

set OUT_DIR=%BUILD_DIR%\test_out
if exist "%OUT_DIR%" rmdir /s /q "%OUT_DIR%"
mkdir "%OUT_DIR%" >nul

set PTX=assets\ptx\demo_kernel.ptx
set PTX_DIVERGE=assets\ptx\demo_divergence.ptx
set PTX_ISA=assets\ptx_isa\demo_ptx64.json
set INST_DESC=assets\inst_desc\demo_desc.json
set CONFIG_JSON=assets\configs\demo_config.json
set PAR_CONFIG_JSON=assets\configs\demo_parallel_config.json
set MODSEL_CONFIG_JSON=assets\configs\demo_modular_selectors.json

echo [integration] smoke run: demo_kernel.ptx
"%CLI%" --ptx "%PTX%" --ptx-isa "%PTX_ISA%" --inst-desc "%INST_DESC%" --config "%CONFIG_JSON%" --trace "%OUT_DIR%\trace.jsonl" --stats "%OUT_DIR%\stats.json" 1>"%OUT_DIR%\stdout.txt" 2>"%OUT_DIR%\stderr.txt"
if errorlevel 1 (
  echo error: smoke run failed (exit=%errorlevel%)
  echo --- stdout (%OUT_DIR%\stdout.txt) ---
  type "%OUT_DIR%\stdout.txt"
  echo --- stderr (%OUT_DIR%\stderr.txt) ---
  type "%OUT_DIR%\stderr.txt"
  popd >nul
  exit /b %errorlevel%
)

for %%F in ("%OUT_DIR%\trace.jsonl" "%OUT_DIR%\stats.json") do (
  if not exist %%~F (
    echo error: missing output file: %%~F
    popd >nul
    exit /b 1
  )
)

findstr /c:"\"action\":\"TRACE_HEADER\"" "%OUT_DIR%\trace.jsonl" >nul
if errorlevel 1 (
  echo error: expected TRACE_HEADER as trace JSONL metadata
  popd >nul
  exit /b 1
)

findstr /c:"\"action\":\"RUN_START\"" "%OUT_DIR%\trace.jsonl" >nul
if errorlevel 1 (
  echo error: expected RUN_START in trace (config_summary missing?)
  popd >nul
  exit /b 1
)

findstr /c:"memory_model" "%OUT_DIR%\trace.jsonl" >nul
if errorlevel 1 (
  echo error: expected memory_model to be observable in RUN_START extra
  popd >nul
  exit /b 1
)

echo [integration] divergence smoke: demo_divergence.ptx (expect SIMT_SPLIT)
"%CLI%" --ptx "%PTX_DIVERGE%" --ptx-isa "%PTX_ISA%" --inst-desc "%INST_DESC%" --config "%CONFIG_JSON%" --block 2,1,1 --trace "%OUT_DIR%\trace_diverge.jsonl" --stats "%OUT_DIR%\stats_diverge.json" 1>"%OUT_DIR%\stdout_diverge.txt" 2>"%OUT_DIR%\stderr_diverge.txt"
if errorlevel 1 (
  echo error: divergence smoke run failed (exit=%errorlevel%)
  echo --- stdout (%OUT_DIR%\stdout_diverge.txt) ---
  type "%OUT_DIR%\stdout_diverge.txt"
  echo --- stderr (%OUT_DIR%\stderr_diverge.txt) ---
  type "%OUT_DIR%\stderr_diverge.txt"
  popd >nul
  exit /b %errorlevel%
)

findstr /c:"\"action\":\"SIMT_SPLIT\"" "%OUT_DIR%\trace_diverge.jsonl" >nul
if errorlevel 1 (
  echo error: expected SIMT_SPLIT in divergence trace
  echo --- tail (%OUT_DIR%\trace_diverge.jsonl) ---
  powershell -NoProfile -Command "Get-Content '%OUT_DIR%\trace_diverge.jsonl' -Tail 100" 2>nul
  popd >nul
  exit /b 1
)

echo [integration] sm parallel smoke: demo_parallel_config.json
"%CLI%" --ptx "%PTX%" --ptx-isa "%PTX_ISA%" --inst-desc "%INST_DESC%" --config "%PAR_CONFIG_JSON%" --grid 4,1,1 --block 32,1,1 --trace "%OUT_DIR%\trace_parallel.jsonl" --stats "%OUT_DIR%\stats_parallel.json" 1>"%OUT_DIR%\stdout_parallel.txt" 2>"%OUT_DIR%\stderr_parallel.txt"
if errorlevel 1 (
  echo error: sm parallel smoke run failed (exit=%errorlevel%)
  echo --- stdout (%OUT_DIR%\stdout_parallel.txt) ---
  type "%OUT_DIR%\stdout_parallel.txt"
  echo --- stderr (%OUT_DIR%\stderr_parallel.txt) ---
  type "%OUT_DIR%\stderr_parallel.txt"
  popd >nul
  exit /b %errorlevel%
)

for %%F in ("%OUT_DIR%\trace_parallel.jsonl" "%OUT_DIR%\stats_parallel.json") do (
  if not exist %%~F (
    echo error: missing output file: %%~F
    popd >nul
    exit /b 1
  )
)

findstr /c:"\"sm_id\":" "%OUT_DIR%\trace_parallel.jsonl" >nul
if errorlevel 1 (
  echo error: expected sm_id in parallel trace (did parallel mode run?)
  popd >nul
  exit /b 1
)

echo [integration] modular selectors smoke: demo_modular_selectors.json
"%CLI%" --ptx "%PTX%" --ptx-isa "%PTX_ISA%" --inst-desc "%INST_DESC%" --config "%MODSEL_CONFIG_JSON%" --grid 4,1,1 --block 32,1,1 --trace "%OUT_DIR%\trace_modsel.jsonl" --stats "%OUT_DIR%\stats_modsel.json" 1>"%OUT_DIR%\stdout_modsel.txt" 2>"%OUT_DIR%\stderr_modsel.txt"
if errorlevel 1 (
  echo error: modular selectors smoke run failed (exit=%errorlevel%)
  echo --- stdout (%OUT_DIR%\stdout_modsel.txt) ---
  type "%OUT_DIR%\stdout_modsel.txt"
  echo --- stderr (%OUT_DIR%\stderr_modsel.txt) ---
  type "%OUT_DIR%\stderr_modsel.txt"
  popd >nul
  exit /b %errorlevel%
)

for %%F in ("%OUT_DIR%\trace_modsel.jsonl" "%OUT_DIR%\stats_modsel.json") do (
  if not exist %%~F (
    echo error: missing output file: %%~F
    popd >nul
    exit /b 1
  )
)

findstr /c:"\"action\":\"RUN_START\"" "%OUT_DIR%\trace_modsel.jsonl" >nul
if errorlevel 1 (
  echo error: expected RUN_START in modular selectors trace
  popd >nul
  exit /b 1
)

findstr /c:"sm_round_robin" "%OUT_DIR%\trace_modsel.jsonl" >nul
if errorlevel 1 (
  echo error: expected cta_scheduler=sm_round_robin to be observable
  popd >nul
  exit /b 1
)

findstr /c:"round_robin_interleave_step" "%OUT_DIR%\trace_modsel.jsonl" >nul
if errorlevel 1 (
  echo error: expected warp_scheduler=round_robin_interleave_step to be observable
  popd >nul
  exit /b 1
)

echo [integration] io-demo: write_out.ptx
"%CLI%" --ptx assets\ptx\write_out.ptx --ptx-isa "%PTX_ISA%" --inst-desc "%INST_DESC%" --config "%CONFIG_JSON%" --trace "%OUT_DIR%\io_trace.jsonl" --stats "%OUT_DIR%\io_stats.json" --io-demo 1>"%OUT_DIR%\io_stdout.txt" 2>"%OUT_DIR%\io_stderr.txt"
if errorlevel 1 (
  echo error: io-demo run failed (exit=%errorlevel%)
  echo --- stdout (%OUT_DIR%\io_stdout.txt) ---
  type "%OUT_DIR%\io_stdout.txt"
  echo --- stderr (%OUT_DIR%\io_stderr.txt) ---
  type "%OUT_DIR%\io_stderr.txt"
  popd >nul
  exit /b %errorlevel%
)

echo [integration] 3D launch smoke: --grid 2,2,1 --block 40,1,1
"%CLI%" --ptx "%PTX%" --ptx-isa "%PTX_ISA%" --inst-desc "%INST_DESC%" --config "%CONFIG_JSON%" --grid 2,2,1 --block 40,1,1 --trace "%OUT_DIR%\trace_3d.jsonl" --stats "%OUT_DIR%\stats_3d.json" 1>"%OUT_DIR%\stdout_3d.txt" 2>"%OUT_DIR%\stderr_3d.txt"
if errorlevel 1 (
  echo error: 3D launch run failed (exit=%errorlevel%)
  echo --- stdout (%OUT_DIR%\stdout_3d.txt) ---
  type "%OUT_DIR%\stdout_3d.txt"
  echo --- stderr (%OUT_DIR%\stderr_3d.txt) ---
  type "%OUT_DIR%\stderr_3d.txt"
  popd >nul
  exit /b %errorlevel%
)

for %%F in ("%OUT_DIR%\trace_3d.jsonl" "%OUT_DIR%\stats_3d.json") do (
  if not exist %%~F (
    echo error: missing output file: %%~F
    popd >nul
    exit /b 1
  )
)

findstr /c:"io-demo u32 result: 42" "%OUT_DIR%\io_stdout.txt" >nul
if errorlevel 1 (
  echo error: expected output not found in io_stdout.txt
  echo --- stdout (%OUT_DIR%\io_stdout.txt) ---
  type "%OUT_DIR%\io_stdout.txt"
  echo --- stderr (%OUT_DIR%\io_stderr.txt) ---
  type "%OUT_DIR%\io_stderr.txt"
  popd >nul
  exit /b 1
)

echo [integration] workload smoke: single stream
"%CLI%" --config "%CONFIG_JSON%" --workload assets\workloads\smoke_single_stream.json --trace "%OUT_DIR%\workload_single.trace.jsonl" --stats "%OUT_DIR%\workload_single.stats.json" 1>"%OUT_DIR%\workload_single.stdout.txt" 2>"%OUT_DIR%\workload_single.stderr.txt"
if errorlevel 1 (
  echo error: workload single-stream run failed (exit=%errorlevel%)
  echo --- stdout (%OUT_DIR%\workload_single.stdout.txt) ---
  type "%OUT_DIR%\workload_single.stdout.txt"
  echo --- stderr (%OUT_DIR%\workload_single.stderr.txt) ---
  type "%OUT_DIR%\workload_single.stderr.txt"
  popd >nul
  exit /b %errorlevel%
)

for %%F in ("%OUT_DIR%\workload_single.trace.jsonl" "%OUT_DIR%\workload_single.stats.json") do (
  if not exist %%~F (
    echo error: missing output file: %%~F
    popd >nul
    exit /b 1
  )
)

findstr /c:"\"cat\":\"STREAM\"" "%OUT_DIR%\workload_single.trace.jsonl" >nul
if errorlevel 1 (
  echo error: expected STREAM events in workload trace
  popd >nul
  exit /b 1
)

findstr /c:"\"action\":\"cmd_enq\"" "%OUT_DIR%\workload_single.trace.jsonl" >nul
if errorlevel 1 (
  echo error: expected cmd_enq in workload trace
  popd >nul
  exit /b 1
)

echo [integration] workload smoke: two streams event_record/wait
"%CLI%" --config "%CONFIG_JSON%" --workload assets\workloads\smoke_two_stream_event.json --trace "%OUT_DIR%\workload_event.trace.jsonl" --stats "%OUT_DIR%\workload_event.stats.json" 1>"%OUT_DIR%\workload_event.stdout.txt" 2>"%OUT_DIR%\workload_event.stderr.txt"
if errorlevel 1 (
  echo error: workload two-stream-event run failed (exit=%errorlevel%)
  echo --- stdout (%OUT_DIR%\workload_event.stdout.txt) ---
  type "%OUT_DIR%\workload_event.stdout.txt"
  echo --- stderr (%OUT_DIR%\workload_event.stderr.txt) ---
  type "%OUT_DIR%\workload_event.stderr.txt"
  popd >nul
  exit /b %errorlevel%
)

for %%F in ("%OUT_DIR%\workload_event.trace.jsonl" "%OUT_DIR%\workload_event.stats.json") do (
  if not exist %%~F (
    echo error: missing output file: %%~F
    popd >nul
    exit /b 1
  )
)

findstr /c:"\"action\":\"event_record\"" "%OUT_DIR%\workload_event.trace.jsonl" >nul
if errorlevel 1 (
  echo error: expected event_record in workload trace
  popd >nul
  exit /b 1
)

findstr /c:"\"action\":\"event_wait_done\"" "%OUT_DIR%\workload_event.trace.jsonl" >nul
if errorlevel 1 (
  echo error: expected event_wait_done in workload trace
  popd >nul
  exit /b 1
)

echo OK
popd >nul
exit /b 0
