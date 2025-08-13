SDKTARGETSYSROOT=/opt/rk3566_host/aarch64-buildroot-linux-gnu/sysroot
CROSS_COMPILE=aarch64-buildroot-linux-gnu-
PATH_EXT=LD_LIBRARY_PATH=/opt/rk3566_host/lib PATH=/opt/rk3566_host/bin:$$PATH
CXX=${PATH_EXT} ${CROSS_COMPILE}g++ --sysroot=${SDKTARGETSYSROOT}
CC=${PATH_EXT} ${CROSS_COMPILE}gcc --sysroot=${SDKTARGETSYSROOT}
STRIP=${PATH_EXT} ${CROSS_COMPILE}strip
AR=${PATH_EXT} ${CROSS_COMPILE}ar
RANLIB=PATH=${PATH_EXT} ${CROSS_COMPILE}ranlib
CXXFLAGS=-Wno-write-strings -ftree-vectorize -ftree-vectorizer-verbose=5 -std=c++11
CFLAGS=-Wno-write-strings -ftree-vectorize -ftree-vectorizer-verbose=5
LDFLAGS=-Wl,-Bsymbolic
LINUX_GNU_LIB=lib

PLATFORM=rk3566_arm64

BUILD_LINUX=ON
BUILD_RK3566_ARM64=ON
