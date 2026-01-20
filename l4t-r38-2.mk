include mkfiles/l4t-r38-2.mk
include mkfiles/funcs.mk

BUILD_WITH_ZZLAB?=ON
BUILD_WITH_QCAP?=ON
BUILD_WITH_CUDA?=ON
BUILD_WITH_CUDA_DRIVER?=ON
BUILD_WITH_SIPL?=ON

include mkfiles/rules.mk
