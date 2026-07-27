#!/bin/bash

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if test -f "/.dockerenv"; then

if [[ "${BUILD_SYSTEM:-make}" == "cmake" ]]; then
    exec "${SCRIPT_DIR}/build-cmake.sh" "$@"
fi

PLATFORM=$1
shift

[[ -f /opt/qcap-dev-init ]] && echo "---- qcap-dev-init ----" && . /opt/qcap-dev-init

time make -f ${PLATFORM}.mk -j $(nproc) $@
# time make -f ${PLATFORM}.mk -j 1 $@

else

echo "Error: build.sh must be run inside the qcap-dev Docker container." >&2
echo "Use: ./scripts/docker-run.sh <image-tag> ./build.sh <platform> [target]" >&2
exit 1

fi

