QCAP_HOME?=/docker/qcap-dev/qcap/build-qcap/_objs/l4t-r38-2/
QCAP_3RDPARTY?=/docker/qcap-3rdparty/l4t-r38-2/

include mkfiles/l4t-r38-2.mk
include mkfiles/funcs.mk

BUILD_WITH_ZZLAB?=ON
BUILD_WITH_QCAP?=ON
BUILD_WITH_CUDA?=ON
BUILD_WITH_CUDA_DRIVER?=ON
BUILD_WITH_SIPL?=ON
BUILD_WITH_IPX?=ON

include mkfiles/rules.mk
