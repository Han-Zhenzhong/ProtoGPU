#!/usr/bin/env bash

# Helper for setting up environment variables to run CUDA demos under gpu-sim's
# CUDA Runtime shim (libcudart.so.12) from this repo's build output.
#
# Usage (recommended):
#   source scripts/setup_cudart_shim_running_env.sh [build_dir] [ptx_override]
#
# Alternative (non-sourced):
#   eval "$(bash scripts/setup_cudart_shim_running_env.sh [build_dir] [ptx_override])"

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
	echo "[cudart-shim-env] $*" >&2
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

_gpusim_prepend_path_var() {
	local key="$1"
	local prefix="$2"
	local current="${!key-}"
	if [[ -z "$current" ]]; then
		_gpusim_set "$key" "$prefix"
	else
		_gpusim_set "$key" "$prefix:$current"
	fi
}

_gpusim_validate_ptx_override_value() {
	local value="$1"
	local delim=':'
	local parts=()
	local part=""

	IFS="$delim" read -r -a parts <<< "$value"
	for part in "${parts[@]}"; do
		part="${part#${part%%[![:space:]]*}}"
		part="${part%${part##*[![:space:]]}}"
		if [[ -z "$part" ]]; then
			_gpusim_die "PTX override contains an empty path element: $value"
		fi
		if [[ ! -f "$part" ]]; then
			_gpusim_die "PTX override file not found: $part"
		fi
	done
}

_gpusim_usage() {
	cat >&2 <<'EOF'
Usage:
	source scripts/setup_cudart_shim_running_env.sh [build_dir] [ptx_override]

Args:
	build_dir      Build dir to search (default: build)
	ptx_override   Optional: path or ':'-delimited path list of text PTX files to set as GPUSIM_CUDART_SHIM_PTX_OVERRIDE

Env:
	CONFIG         Multi-config subdir name (default: Release)

Examples:
	source scripts/setup_cudart_shim_running_env.sh build
	./cuda/demo/demo

	source scripts/setup_cudart_shim_running_env.sh build "$PWD/cuda/demo/streaming_demo.ptx"
	./cuda/demo/streaming_demo

	source scripts/setup_cudart_shim_running_env.sh build "$PWD/a.ptx:$PWD/b.ptx"
	./cuda/demo/streaming_demo
EOF
}

BUILD_DIR="${1:-build}"
CONFIG="${CONFIG:-Release}"
PTX_OVERRIDE_PATH="${2:-}"

cd "$_GPUSIM_REPO_ROOT" || _gpusim_die "failed to cd to repo root: $_GPUSIM_REPO_ROOT"

if [[ "$(uname -s)" != "Linux"* ]]; then
	_gpusim_note "warning: this shim demo path primarily targets Linux/WSL"
fi

SHIM_DIR=""
for d in "$BUILD_DIR" "$BUILD_DIR/$CONFIG"; do
	if [[ -f "$d/libcudart.so.12" ]]; then
		SHIM_DIR="$d"
		break
	fi
done

if [[ -z "$SHIM_DIR" ]]; then
	_gpusim_usage
	_gpusim_die "cannot find libcudart.so.12 under $BUILD_DIR (config=$CONFIG)"
fi

_gpusim_set "GPUSIM_CUDART_SHIM_DIR" "$_GPUSIM_REPO_ROOT/$SHIM_DIR"
_gpusim_prepend_path_var "LD_LIBRARY_PATH" "$_GPUSIM_REPO_ROOT/$SHIM_DIR"

if [[ -n "$PTX_OVERRIDE_PATH" ]]; then
	_gpusim_validate_ptx_override_value "$PTX_OVERRIDE_PATH"
	_gpusim_set "GPUSIM_CUDART_SHIM_PTX_OVERRIDE" "$PTX_OVERRIDE_PATH"
fi

# Optional: point the shim at the repo demo assets if they exist and callers
# haven't already overridden them.
if [[ -z "${GPUSIM_CONFIG:-}" && -f "$_GPUSIM_REPO_ROOT/assets/configs/demo_config.json" ]]; then
	_gpusim_set "GPUSIM_CONFIG" "$_GPUSIM_REPO_ROOT/assets/configs/demo_config.json"
fi
if [[ -z "${GPUSIM_PTX_ISA:-}" && -f "$_GPUSIM_REPO_ROOT/assets/ptx_isa/demo_ptx64.json" ]]; then
	_gpusim_set "GPUSIM_PTX_ISA" "$_GPUSIM_REPO_ROOT/assets/ptx_isa/demo_ptx64.json"
fi
if [[ -z "${GPUSIM_INST_DESC:-}" && -f "$_GPUSIM_REPO_ROOT/assets/inst_desc/demo_desc.json" ]]; then
	_gpusim_set "GPUSIM_INST_DESC" "$_GPUSIM_REPO_ROOT/assets/inst_desc/demo_desc.json"
fi

if [[ $_GPUSIM_IS_SOURCED -eq 1 ]]; then
	_gpusim_note "set LD_LIBRARY_PATH to include: $_GPUSIM_REPO_ROOT/$SHIM_DIR"
	if [[ -n "$PTX_OVERRIDE_PATH" ]]; then
		_gpusim_note "set GPUSIM_CUDART_SHIM_PTX_OVERRIDE=$PTX_OVERRIDE_PATH"
	elif [[ -n "${GPUSIM_CUDART_SHIM_PTX_OVERRIDE:-}" ]]; then
		_gpusim_note "using existing GPUSIM_CUDART_SHIM_PTX_OVERRIDE=${GPUSIM_CUDART_SHIM_PTX_OVERRIDE}"
	fi
else
	# When executed for eval, print nothing else on stdout.
	true
fi

