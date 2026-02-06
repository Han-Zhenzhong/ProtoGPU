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
  echo [unit] running via ctest (build dir: %BUILD_DIR%, config: %CONFIG%)
  ctest --test-dir "%BUILD_DIR%" -C %CONFIG% -V
  exit /b %errorlevel%
)

set EXE=
for %%P in (
  "%BUILD_DIR%\gpu-sim-tests.exe"
  "%BUILD_DIR%\%CONFIG%\gpu-sim-tests.exe"
) do (
  if exist %%~P set EXE=%%~P
)

if "%EXE%"=="" (
  echo error: cannot find gpu-sim-tests.exe under %BUILD_DIR% (config=%CONFIG%)
  exit /b 2
)

echo [unit] running: %EXE%
"%EXE%"
exit /b %errorlevel%
