#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

BUILD_DIR="${1:-build}"
CONFIG="${2:-${CONFIG:-Release}}"

# BUILD_TESTING defaults to ON so unit tests are built.
BUILD_TESTING="${BUILD_TESTING:-ON}"

GENERATOR_ARGS=()
if [[ -n "${GENERATOR:-}" ]]; then
  GENERATOR_ARGS=(-G "$GENERATOR")
else
  if command -v ninja >/dev/null 2>&1; then
    GENERATOR_ARGS=(-G Ninja)
  fi
fi

cd "$REPO_ROOT"

if [[ ! -f "$BUILD_DIR/CMakeCache.txt" ]]; then
  echo "[build] configuring (dir: $BUILD_DIR, config: $CONFIG, BUILD_TESTING=$BUILD_TESTING)"
  cmake -S . -B "$BUILD_DIR" "${GENERATOR_ARGS[@]}" \
    -DBUILD_TESTING="$BUILD_TESTING" \
    -DCMAKE_BUILD_TYPE="$CONFIG"
else
  echo "[build] already configured: $BUILD_DIR"
fi

echo "[build] building (dir: $BUILD_DIR, config: $CONFIG)"
cmake --build "$BUILD_DIR" --config "$CONFIG"
