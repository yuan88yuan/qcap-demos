# Toolchain configured by /opt/qcap-dev-init
NOWARN=-Wno-nonnull -Wno-deprecated-declarations
CXXFLAGS+=$(NOWARN)
CFLAGS+=$(NOWARN)

QCAP_HOME?=/docker/qcap-dev/qcap/build-qcap/_objs/xlnk2_arm64/
QCAP_3RDPARTY?=/docker/qcap-3rdparty/xlnk2_arm64/

LINUX_GNU_LIB=lib

PLATFORM=xlnk2_arm64

BUILD_LINUX=ON
BUILD_XLNK2_ARM64=ON
