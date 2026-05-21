#!/bin/bash

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if test -f "/.dockerenv"; then

PLATFORM=$1
shift

[[ -f /opt/qcap-dev-init ]] && echo "---- qcap-dev-init ----" && . /opt/qcap-dev-init

time make -f ${PLATFORM}.mk -j $(nproc) $@
# time make -f ${PLATFORM}.mk -j 1 $@

else

eval "docker exec $1 su $USER -c '/docker/qcap-demos/build.sh $@'"

fi

