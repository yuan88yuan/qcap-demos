include mkfiles/l4t-r35-1.mk
include mkfiles/funcs.mk

BUILD_WITH_ZZLAB?=ON
BUILD_WITH_QCAP?=ON
BUILD_WITH_CUDA?=ON
BUILD_WITH_NPP?=ON
BUILD_WITH_NVBUF?=ON

include mkfiles/rules.mk
