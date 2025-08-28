include mkfiles/igx-r36-3.mk
include mkfiles/funcs.mk

BUILD_WITH_ZZLAB?=ON
BUILD_WITH_QCAP?=ON
BUILD_WITH_CUDA?=ON
BUILD_WITH_NPP?=ON
BUILD_WITH_NVBUF?=ON
BUILD_WITH_FMT?=ON
BUILD_WITH_IBVERBS?=ON

include mkfiles/rules.mk
