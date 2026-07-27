#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [[ ! -f /.dockerenv ]]; then
    echo "Error: build-cmake.sh must be run inside the qcap-dev Docker container." >&2
    echo "Use: ./scripts/docker-run.sh <image-tag> ./build-cmake.sh <platform> [target ...]" >&2
    exit 1
fi

if [[ $# -lt 1 ]]; then
    echo "Usage: ./build-cmake.sh <platform> [target ...]" >&2
    exit 1
fi

PLATFORM="$1"
shift
BUILD_DIR="${SCRIPT_DIR}/_objs/${PLATFORM}"

[[ -f /opt/qcap-dev-init ]] && echo "---- qcap-dev-init ----" && . /opt/qcap-dev-init

CMAKE_ARGS=(
    -DQCAP_PLATFORM="${PLATFORM}"
)
if [[ "${PLATFORM}" == "xlnk2_arm64" ]]; then
    CMAKE_ARGS+=("-DCMAKE_TOOLCHAIN_FILE=${SCRIPT_DIR}/cmake/platforms/${PLATFORM}.cmake")
fi
if [[ -n "${QCAP_HOME:-}" ]]; then
    CMAKE_ARGS+=("-DQCAP_HOME=${QCAP_HOME}")
fi
if [[ -n "${SDKTARGETSYSROOT:-}" ]]; then
    CMAKE_ARGS+=("-DSDKTARGETSYSROOT=${SDKTARGETSYSROOT}")
fi

if [[ "${1:-}" == "distclean" ]]; then
    rm -rf "${BUILD_DIR}"
    shift
    [[ $# -eq 0 ]] && exit 0
fi
if [[ "${1:-}" == "reconfigure" ]]; then
    rm -f "${BUILD_DIR}/CMakeCache.txt"
    rm -rf "${BUILD_DIR}/CMakeFiles"
    shift
    [[ $# -eq 0 ]] && set -- all
fi

# Optional, space-separated configure arguments for feature/platform overrides.
if [[ -n "${CMAKE_ARGS_EXTRA:-}" ]]; then
    read -r -a EXTRA_ARGS <<< "${CMAKE_ARGS_EXTRA}"
    CMAKE_ARGS+=("${EXTRA_ARGS[@]}")
fi

cmake -S "${SCRIPT_DIR}" -B "${BUILD_DIR}" "${CMAKE_ARGS[@]}"

if [[ $# -eq 0 ]]; then
    set -- all
fi

cmake --build "${BUILD_DIR}" --parallel "$(nproc)" --target "$@"
