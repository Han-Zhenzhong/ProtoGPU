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

where ctest >nul 2>nul
if %errorlevel%==0 (
  echo [unit] running via ctest (unit-only; build dir: %BUILD_DIR%, config: %CONFIG%)
  ctest --test-dir "%BUILD_DIR%" -C %CONFIG% -V -R "^gpu-sim-(tests|builtins-tests|config-parse-tests|tiny-gpt2-mincov-tests)$"
  exit /b %errorlevel%
)

set FAIL=0
set RAN_ANY=0

for %%N in (
  gpu-sim-tests
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
  exit /b 2
)

exit /b %FAIL%
