@echo off
setlocal enabledelayedexpansion

set BUILD_DIR=%~1
if "%BUILD_DIR%"=="" set BUILD_DIR=build

if "%CONFIG%"=="" (
  set CONFIG=Release
)

if not exist "%BUILD_DIR%" (
  echo error: build dir not found: %BUILD_DIR%
  exit /b 2
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
  exit /b 2
)

set OUT_DIR=%BUILD_DIR%\test_out
if exist "%OUT_DIR%" rmdir /s /q "%OUT_DIR%"
mkdir "%OUT_DIR%" >nul

set PTX=assets\ptx\demo_kernel.ptx
set PTX_ISA=assets\ptx_isa\demo_ptx8.json
set INST_DESC=assets\inst_desc\demo_desc.json
set CONFIG_JSON=assets\configs\demo_config.json

echo [integration] smoke run: demo_kernel.ptx
"%CLI%" --ptx "%PTX%" --ptx-isa "%PTX_ISA%" --inst-desc "%INST_DESC%" --config "%CONFIG_JSON%" --trace "%OUT_DIR%\trace.jsonl" --stats "%OUT_DIR%\stats.json" 1>"%OUT_DIR%\stdout.txt" 2>"%OUT_DIR%\stderr.txt"
if errorlevel 1 (
  echo error: smoke run failed (exit=%errorlevel%)
  echo --- stdout (%OUT_DIR%\stdout.txt) ---
  type "%OUT_DIR%\stdout.txt"
  echo --- stderr (%OUT_DIR%\stderr.txt) ---
  type "%OUT_DIR%\stderr.txt"
  exit /b %errorlevel%
)

for %%F in ("%OUT_DIR%\trace.jsonl" "%OUT_DIR%\stats.json") do (
  if not exist %%~F (
    echo error: missing output file: %%~F
    exit /b 1
  )
)

echo [integration] io-demo: write_out.ptx
"%CLI%" --ptx assets\ptx\write_out.ptx --ptx-isa "%PTX_ISA%" --inst-desc "%INST_DESC%" --config "%CONFIG_JSON%" --trace "%OUT_DIR%\io_trace.jsonl" --stats "%OUT_DIR%\io_stats.json" --io-demo 1>"%OUT_DIR%\io_stdout.txt" 2>"%OUT_DIR%\io_stderr.txt"
if errorlevel 1 (
  echo error: io-demo run failed (exit=%errorlevel%)
  echo --- stdout (%OUT_DIR%\io_stdout.txt) ---
  type "%OUT_DIR%\io_stdout.txt"
  echo --- stderr (%OUT_DIR%\io_stderr.txt) ---
  type "%OUT_DIR%\io_stderr.txt"
  exit /b %errorlevel%
)

findstr /c:"io-demo u32 result: 42" "%OUT_DIR%\io_stdout.txt" >nul
if errorlevel 1 (
  echo error: expected output not found in io_stdout.txt
  echo --- stdout (%OUT_DIR%\io_stdout.txt) ---
  type "%OUT_DIR%\io_stdout.txt"
  echo --- stderr (%OUT_DIR%\io_stderr.txt) ---
  type "%OUT_DIR%\io_stderr.txt"
  exit /b 1
)

echo OK
exit /b 0
