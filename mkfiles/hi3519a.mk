SDKTARGETSYSROOT=/opt/hisi-linux/x86-arm/arm-himix200-linux/target
CROSS_COMPILE=arm-himix200-linux-
PATH_EXT=PATH=/opt/hisi-linux/x86-arm/arm-himix200-linux/bin:$$PATH
CXX=${PATH_EXT} ${CROSS_COMPILE}g++ --sysroot=${SDKTARGETSYSROOT}
CC=${PATH_EXT} ${CROSS_COMPILE}gcc --sysroot=${SDKTARGETSYSROOT}
STRIP=${PATH_EXT} ${CROSS_COMPILE}strip
AR=${PATH_EXT} ${CROSS_COMPILE}ar
RANLIB=${PATH_EXT} ${CROSS_COMPILE}ranlib
CXXFLAGS=-fpermissive -mno-unaligned-access -fno-aggressive-loop-optimizations -Wno-literal-suffix -Wno-write-strings -Wno-int-to-pointer-cast -mcpu=cortex-a9 -mfloat-abi=softfp -mfpu=neon -std=c++11
CFLAGS=-mno-unaligned-access -fno-aggressive-loop-optimizations -Wno-write-strings -mcpu=cortex-a9 -mfloat-abi=softfp -mfpu=neon
LDFLAGS=-Wl,-Bsymbolic -mcpu=cortex-a9 -mfloat-abi=softfp -mfpu=neon -std=c++11
MPI_HOME=/opt/hi3519a-sdk/Hi3519AV100_SDK_V2.0.2.0/smp/a53_linux/mpp
LINUX_GNU_LIB=lib

PLATFORM=hi3519a

BUILD_HISIV=ON
BUILD_HI3519A=ON
