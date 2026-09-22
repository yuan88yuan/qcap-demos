QCAP_HOME?=../qcap-dev/qcap/build-qcap/_objs/debian136_x64

include mkfiles/debian136_x64.mk
include mkfiles/funcs.mk

BUILD_WITH_ZZLAB?=ON
BUILD_WITH_QCAP?=ON
BUILD_WITH_QCAP2_LIC?=ON

BUILD_WITH_FONTCONFIG?=ON
BUILD_WITH_FREETYPE?=ON
BUILD_WITH_HARFBUZZ?=ON

include mkfiles/rules.mk
