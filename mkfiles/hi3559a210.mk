SDKTARGETSYSROOT=/opt/hisi-linux/x86-arm/aarch64-himix210-linux/target
CROSS_COMPILE=aarch64-himix210-linux-
PATH_EXT=PATH=/opt/hisi-linux/x86-arm/gcc-arm-none-eabi-4_9-2015q3/bin:/opt/hisi-linux/x86-arm/aarch64-himix210-linux/bin:$$PATH
CXX=${PATH_EXT} ${CROSS_COMPILE}g++ --sysroot=${SDKTARGETSYSROOT}
CC=${PATH_EXT} ${CROSS_COMPILE}gcc --sysroot=${SDKTARGETSYSROOT}
STRIP=${PATH_EXT} ${CROSS_COMPILE}strip
AR=${PATH_EXT} ${CROSS_COMPILE}ar
RANLIB=${PATH_EXT} ${CROSS_COMPILE}ranlib
CXXFLAGS=-fpermissive -Wno-literal-suffix -Wno-write-strings -Wno-int-to-pointer-cast
CFLAGS=-Wno-write-strings
LDFLAGS=-Wl,-Bsymbolic
MPI_HOME=/opt/hi3559a-sdk/Hi3559AV100_SDK_V2.0.4.0/mpp/linux/multi-core
LINUX_GNU_LIB=lib

PLATFORM=hi3559a210

BUILD_HISIV=ON
BUILD_HI3559A=ON
BUILD_HI3559A210=ON
