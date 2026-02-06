@echo off
setlocal enabledelayedexpansion

set BUILD_DIR=%~1
if "%BUILD_DIR%"=="" set BUILD_DIR=build

set CONFIG=%~2
if "%CONFIG%"=="" (
  if "%CONFIG%"=="" set CONFIG=Release
)

if "%BUILD_TESTING%"=="" set BUILD_TESTING=ON

echo [build] configuring (dir: %BUILD_DIR%, config: %CONFIG%, BUILD_TESTING=%BUILD_TESTING%)
if not exist "%BUILD_DIR%\CMakeCache.txt" (
  cmake -S . -B "%BUILD_DIR%" -DBUILD_TESTING=%BUILD_TESTING%
  if errorlevel 1 exit /b %errorlevel%
)

echo [build] building (dir: %BUILD_DIR%, config: %CONFIG%)
cmake --build "%BUILD_DIR%" --config %CONFIG%
exit /b %errorlevel%
