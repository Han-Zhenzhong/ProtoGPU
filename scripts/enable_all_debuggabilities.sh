#!/usr/bin/env bash

# Enable all currently-implemented debug toggles via environment variables.
#
# Usage (recommended):
#   source scripts/enable_all_debuggabilities.sh [build_dir]
#
# Alternative (non-sourced):
#   eval "$(bash scripts/enable_all_debuggabilities.sh [build_dir])"
#
# Notes:
# - This only enables env vars that are currently recognized by the codebase.
# - Dump paths are written under <build_dir>/test_out/ by default.

_GPUSIM_SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
_GPUSIM_REPO_ROOT="$(cd "$_GPUSIM_SCRIPT_DIR/.." && pwd)"

_GPUSIM_IS_SOURCED=0
if [[ "${BASH_SOURCE[0]}" != "${0}" ]]; then
	_GPUSIM_IS_SOURCED=1
fi

if [[ $_GPUSIM_IS_SOURCED -eq 0 ]]; then
	set -euo pipefail
fi

_gpusim_die() {
	local msg="$1"
	if [[ $_GPUSIM_IS_SOURCED -eq 1 ]]; then
		echo "error: $msg" >&2
		return 2
	fi
	echo "error: $msg" >&2
	exit 2
}

_gpusim_note() {
	# informational messages always go to stderr so stdout can be used for exports
	echo "[gpusim-debug] $*" >&2
}

_gpusim_set() {
	# When sourced, export directly; otherwise print shell-safe exports.
	local key="$1"
	local val="$2"
	if [[ $_GPUSIM_IS_SOURCED -eq 1 ]]; then
		export "$key=$val"
	else
		printf 'export %s=%q\n' "$key" "$val"
	fi
}

_gpusim_usage() {
	cat >&2 <<'EOF'
Usage:
	source scripts/enable_all_debuggabilities.sh [build_dir]

Args:
	build_dir   Output directory for logs/dumps (default: build)

Examples:
	source scripts/enable_all_debuggabilities.sh build
	bash scripts/run_cuda_shim_e2e_demo_integration.sh build

	eval "$(bash scripts/enable_all_debuggabilities.sh build)"
	./cuda/demo/demo
EOF
}

BUILD_DIR="${1:-build}"

cd "$_GPUSIM_REPO_ROOT" || _gpusim_die "failed to cd to repo root: $_GPUSIM_REPO_ROOT"

OUT_DIR="$BUILD_DIR/test_out"
mkdir -p "$OUT_DIR" || _gpusim_die "failed to create out dir: $OUT_DIR"

# --- Core (gpu-sim) debug logging toggles ---
_gpusim_set "GPUSIM_LOG_WARPS" "1"
_gpusim_set "GPUSIM_LOG_GLOBAL_STORES" "1"
_gpusim_set "GPUSIM_LOG_BUILTINS" "1"

# --- CUDA Runtime shim debug toggles ---
_gpusim_set "GPUSIM_CUDART_SHIM_LOG" "1"
_gpusim_set "GPUSIM_CUDART_SHIM_LOG_RUNTIME" "1"
_gpusim_set "GPUSIM_CUDART_SHIM_LOG_LAUNCH" "1"
_gpusim_set "GPUSIM_CUDART_SHIM_LOG_ARGS" "1"

# --- CUDA Runtime shim dump aids ---
# These help debug fatbin parsing and PTX extraction.
_gpusim_set "GPUSIM_CUDART_SHIM_DUMP_FATBIN_WRAPPER" "$OUT_DIR/cudart_shim_fatbin_wrapper.bin"
_gpusim_set "GPUSIM_CUDART_SHIM_DUMP_FATBIN" "$OUT_DIR/cudart_shim_fatbin.bin"
_gpusim_set "GPUSIM_CUDART_SHIM_DUMP_PTX" "$OUT_DIR/cudart_shim_extracted"

if [[ $_GPUSIM_IS_SOURCED -eq 1 ]]; then
	_gpusim_note "enabled core + cudart-shim debug logging"
	_gpusim_note "dump outputs under: $OUT_DIR"
else
	# When executed for eval, print nothing else on stdout.
	true
fi

