SDKTARGETSYSROOT=/opt/hisi-linux/x86-arm/arm-hisiv300-linux/target
CROSS_COMPILE=arm-hisiv300-linux-
PATH_EXT=PATH=/opt/hisi-linux/x86-arm/arm-hisiv300-linux/target/bin:$$PATH
CXX=${PATH_EXT} ${CROSS_COMPILE}g++ --sysroot=${SDKTARGETSYSROOT}
CC=${PATH_EXT} ${CROSS_COMPILE}gcc --sysroot=${SDKTARGETSYSROOT}
STRIP=${PATH_EXT} ${CROSS_COMPILE}strip
AR=${PATH_EXT} ${CROSS_COMPILE}ar
RANLIB=${PATH_EXT} ${CROSS_COMPILE}ranlib
CXXFLAGS=-fpermissive -Wno-literal-suffix -Wno-write-strings -Wno-int-to-pointer-cast -mcpu=cortex-a9 -mfloat-abi=softfp -mfpu=neon -std=c++11
CFLAGS=-Wno-write-strings -mcpu=cortex-a9 -mfloat-abi=softfp -mfpu=neon
LDFLAGS=-Wl,-Bsymbolic -mcpu=cortex-a9 -mfloat-abi=softfp -mfpu=neon
MPI_HOME=/opt/hi3531a-sdk/Hi3531A_SDK_V1.0.2.0/mpp
LINUX_GNU_LIB=lib

PLATFORM=hi3531a

BUILD_HISIV=ON
BUILD_HI3531A=ON
