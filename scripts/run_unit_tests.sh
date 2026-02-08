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
  echo "[unit] running via ctest (unit-only; build dir: $BUILD_DIR, config: $CONFIG)"
  ctest --test-dir "$BUILD_DIR" -C "$CONFIG" -V -R "^gpu-sim-(tests|inst-desc-tests|simt-tests|memory-tests|observability-contract-tests|public-api-tests|builtins-tests|config-parse-tests|tiny-gpt2-mincov-tests)$"
  exit 0
fi

echo "[unit] ctest not found; running unit test executables directly" >&2

TESTS=(
  "gpu-sim-tests"
  "gpu-sim-inst-desc-tests"
  "gpu-sim-simt-tests"
  "gpu-sim-memory-tests"
  "gpu-sim-observability-contract-tests"
  "gpu-sim-public-api-tests"
  "gpu-sim-builtins-tests"
  "gpu-sim-config-parse-tests"
  "gpu-sim-tiny-gpt2-mincov-tests"
)

ran_any=0
for name in "${TESTS[@]}"; do
  CANDIDATES=(
    "$BUILD_DIR/$name"
    "$BUILD_DIR/$name.exe"
    "$BUILD_DIR/$CONFIG/$name"
    "$BUILD_DIR/$CONFIG/$name.exe"
  )

  found=""
  for exe in "${CANDIDATES[@]}"; do
    if [[ -f "$exe" ]]; then
      found="$exe"
      break
    fi
  done

  if [[ -n "$found" ]]; then
    ran_any=1
    echo "[unit] running: $found"
    "$found"
  fi
done

if [[ $ran_any -eq 0 ]]; then
  echo "error: cannot find unit test executables under $BUILD_DIR (config=$CONFIG)" >&2
  exit 2
fi

echo "OK"
