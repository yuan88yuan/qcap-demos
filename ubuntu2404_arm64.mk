QCAP_HOME?=/docker/qcap-dev/qcap/build-qcap/_objs/ubuntu2404_arm64/
QCAP_3RDPARTY?=/docker/qcap-3rdparty/ubuntu2404_arm64/

include mkfiles/ubuntu2404_arm64.mk
include mkfiles/funcs.mk

BUILD_WITH_ZZLAB?=ON
BUILD_WITH_QCAP?=ON
BUILD_WITH_QCAP2_LIC?=ON
BUILD_WITH_CUDA?=ON
BUILD_WITH_CUDA_DRIVER?=ON

include mkfiles/rules.mk
