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

# If the test binary is missing, we also need a build.
if [[ $need_build -eq 0 ]]; then
  if [[ ! -f "$BUILD_DIR/gpu-sim-tests" && ! -f "$BUILD_DIR/gpu-sim-tests.exe" && ! -f "$BUILD_DIR/$CONFIG/gpu-sim-tests" && ! -f "$BUILD_DIR/$CONFIG/gpu-sim-tests.exe" ]]; then
    need_build=1
  fi
fi

if [[ $need_build -eq 1 ]]; then
  echo "[unit] build artifacts missing; building first"
  BUILD_TESTING=ON bash "$SCRIPT_DIR/build.sh" "$BUILD_DIR" "$CONFIG"
fi

if command -v ctest >/dev/null 2>&1; then
  echo "[unit] running via ctest (build dir: $BUILD_DIR, config: $CONFIG)"
  ctest --test-dir "$BUILD_DIR" -C "$CONFIG" -V
  exit 0
fi

echo "[unit] ctest not found; trying to run gpu-sim-tests directly" >&2

CANDIDATES=(
  "$BUILD_DIR/gpu-sim-tests"
  "$BUILD_DIR/gpu-sim-tests.exe"
  "$BUILD_DIR/$CONFIG/gpu-sim-tests"
  "$BUILD_DIR/$CONFIG/gpu-sim-tests.exe"
)

for exe in "${CANDIDATES[@]}"; do
  if [[ -f "$exe" ]]; then
    echo "[unit] running: $exe"
    "$exe"
    exit 0
  fi
done

echo "error: cannot find gpu-sim-tests under $BUILD_DIR (config=$CONFIG)" >&2
exit 2
