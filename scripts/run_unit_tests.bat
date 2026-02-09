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
  if not exist "%BUILD_DIR%\gpu-sim-tiny-gpt2-mincov-tests.exe" if not exist "%BUILD_DIR%\%CONFIG%\gpu-sim-tiny-gpt2-mincov-tests.exe" set NEED_BUILD=1
)

if %NEED_BUILD%==1 (
  echo [unit] build artifacts missing; building first
  set BUILD_TESTING=ON
  call "%SCRIPT_DIR%\build.bat" "%BUILD_DIR%" "%CONFIG%"
  if errorlevel 1 (
    popd >nul
    exit /b %errorlevel%
  )
)

where ctest >nul 2>nul
if %errorlevel%==0 (
  echo [unit] running via ctest (unit-only; build dir: %BUILD_DIR%, config: %CONFIG%)
  ctest --test-dir "%BUILD_DIR%" -C %CONFIG% -V -R "^gpu-sim-(tests|inst-desc-tests|simt-tests|memory-tests|observability-contract-tests|public-api-tests|builtins-tests|config-parse-tests|tiny-gpt2-mincov-tests|cudart-shim-smoke-tests|cudart-shim-streaming-demo)$"
  popd >nul
  exit /b %errorlevel%
)

set FAIL=0
set RAN_ANY=0

for %%N in (
  gpu-sim-tests
  gpu-sim-inst-desc-tests
  gpu-sim-simt-tests
  gpu-sim-memory-tests
  gpu-sim-observability-contract-tests
  gpu-sim-public-api-tests
  gpu-sim-builtins-tests
  gpu-sim-config-parse-tests
  gpu-sim-tiny-gpt2-mincov-tests
) do (
  set EXE=
  for %%P in (
    "%BUILD_DIR%\%%N.exe"
    "%BUILD_DIR%\%CONFIG%\%%N.exe"
  ) do (
    if exist %%~P set EXE=%%~P
  )

  if not "!EXE!"=="" (
    set RAN_ANY=1
    echo [unit] running: !EXE!
    "!EXE!"
    if errorlevel 1 (
      set FAIL=!errorlevel!
      goto :done
    )
  )
)

:done
if %RAN_ANY%==0 (
  echo error: cannot find unit test executables under %BUILD_DIR% (config=%CONFIG%)
  popd >nul
  exit /b 2
)

popd >nul
exit /b %FAIL%
