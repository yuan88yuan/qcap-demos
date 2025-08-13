# Toolchain configured by /opt/qcap-dev-init
# CFLAGS=-march=armv7-a -mtune=cortex-a9 -mfpu=neon -mfloat-abi=hard -ftree-vectorize -fno-builtin -fno-common -Wformat=1 -D_BSP_NA51102_
# CXXFLAGS=${CFLAGS} -std=c++11
# LDFLAGS=-Wl,-O1 -Wl,--hash-style=gnu -Wl,--as-needed
LINUX_GNU_LIB=lib

PLATFORM=novatek_arm64

BUILD_LINUX=ON
BUILD_NOVATEK=ON
BUILD_NOVATEK_ARM64=ON
