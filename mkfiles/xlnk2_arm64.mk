# Toolchain configured by /opt/qcap-dev-init
NOWARN=-Wno-nonnull -Wno-deprecated-declarations
CXXFLAGS+=$(NOWARN)
CFLAGS+=$(NOWARN)

LINUX_GNU_LIB=lib

PLATFORM=xlnk2_arm64

BUILD_LINUX=ON
BUILD_XLNK2_ARM64=ON
