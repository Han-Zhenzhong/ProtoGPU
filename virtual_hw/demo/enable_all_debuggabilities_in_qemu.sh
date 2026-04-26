#!/bin/sh

# Enable all currently-implemented debug toggles via environment variables.
#
# POSIX sh compatible (BusyBox ash friendly).
#
# Usage (recommended):
#   eval "$(sh virtual_hw/demo/enable_all_debuggabilities_in_qemu.sh [build_dir])"
#
# Run one command with vars applied:
#   sh virtual_hw/demo/enable_all_debuggabilities_in_qemu.sh [build_dir] -- <command> [args...]
#
# Notes:
# - This only enables env vars that are currently recognized by the codebase.
# - Dump paths are written under <build_dir>/test_out/ by default.

set -eu

_gpusim_die() {
	msg="$1"
	echo "error: $msg" >&2
	exit 2
}

_gpusim_note() {
	# informational messages always go to stderr so stdout can be used for exports
	echo "[gpusim-debug] $*" >&2
}

_gpusim_set() {
	# Export in current process and collect shell-safe exports for eval output.
	key="$1"
	val="$2"
	export "$key=$val"
	_GPUSIM_EXPORT_LINES="${_GPUSIM_EXPORT_LINES}export ${key}='$(printf "%s" "$val" | sed "s/'/'\\''/g")'
"
}

_gpusim_usage() {
	cat >&2 <<'EOF'
Usage:
	eval "$(sh virtual_hw/demo/enable_all_debuggabilities_in_qemu.sh [build_dir])"
	sh virtual_hw/demo/enable_all_debuggabilities_in_qemu.sh [build_dir] -- <command> [args...]

Args:
	build_dir   Output directory for logs/dumps (default: build)

Examples:
	eval "$(sh virtual_hw/demo/enable_all_debuggabilities_in_qemu.sh build)"
	sh virtual_hw/demo/enable_all_debuggabilities_in_qemu.sh build -- ./build/protogpu-vhw-brokerd /tmp/protogpu-broker.sock
EOF
}

BUILD_DIR="${1:-build}"
shift || true

RUN_MODE=0
if [ "${1:-}" = "--" ]; then
	RUN_MODE=1
	shift
fi

case "$BUILD_DIR" in
	-h|--help)
		_gpusim_usage
		exit 0
		;;
esac

OUT_DIR="/tmp/gpusim-debug-outputs"
mkdir -p "$OUT_DIR" || _gpusim_die "failed to create out dir: $OUT_DIR"

_GPUSIM_EXPORT_LINES=""

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

if [ "$RUN_MODE" -eq 1 ]; then
	if [ "$#" -eq 0 ]; then
		_gpusim_die "missing command after --"
	fi
	_gpusim_note "enabled core + cudart-shim debug logging"
	_gpusim_note "dump outputs under: $OUT_DIR"
	exec "$@"
else
	# Print exports for eval use; keep logs on stderr only.
	printf "%s" "$_GPUSIM_EXPORT_LINES"
fi

