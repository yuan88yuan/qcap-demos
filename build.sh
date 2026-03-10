#!/bin/bash

if test -f "/.dockerenv"; then

PLATFORM=$1
shift

[[ -f /opt/qcap-dev-init ]] && echo "---- qcap-dev-init ----" && . /opt/qcap-dev-init

cd /docker/qcap-demos/
# time make -f ${PLATFORM}.mk -j $(nproc) $@
time make -f ${PLATFORM}.mk -j 4 $@

else

eval "docker exec $1 su $USER -c '/docker/qcap-demos/build.sh $@'"

fi

