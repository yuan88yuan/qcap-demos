SDKTARGETSYSROOT=/opt/l4t/rootfs
CROSS_COMPILE=aarch64-linux-gnu-
PATH_EXT=PATH=/opt/l4t/toolchain/bin:$$PATH
CXX=${PATH_EXT} ${CROSS_COMPILE}g++ --sysroot=${SDKTARGETSYSROOT}
CC=${PATH_EXT} ${CROSS_COMPILE}gcc --sysroot=${SDKTARGETSYSROOT}
STRIP=${PATH_EXT} ${CROSS_COMPILE}strip
AR=${PATH_EXT} ${CROSS_COMPILE}ar
RANLIB=${PATH_EXT} ${CROSS_COMPILE}ranlib
CXXFLAGS=-std=c++11 -I${SDKTARGETSYSROOT}/usr/include/aarch64-linux-gnu
CFLAGS=-I${SDKTARGETSYSROOT}/usr/include/aarch64-linux-gnu
LDFLAGS=-L${SDKTARGETSYSROOT}/lib/aarch64-linux-gnu -Wl,-rpath-link=${SDKTARGETSYSROOT}/lib/aarch64-linux-gnu \
-Wl,-O1 -Wl,--hash-style=gnu -Wl,--as-needed
LINUX_GNU_LIB=lib/aarch64-linux-gnu

NVCC=${PATH_EXT} /usr/local/cuda/bin/nvcc -ccbin ${CROSS_COMPILE}g++

# CUDA code generation flags
GENCODE_SM53=-gencode arch=compute_53,code=sm_53
GENCODE_SM62=-gencode arch=compute_62,code=sm_62
GENCODE_SM72=-gencode arch=compute_72,code=sm_72
GENCODE_SM87=-gencode arch=compute_87,code=sm_87
GENCODE_SM_PTX=-gencode arch=compute_72,code=compute_72
GENCODE_FLAGS=$(GENCODE_SM53) $(GENCODE_SM62) $(GENCODE_SM72) $(GENCODE_SM87) $(GENCODE_SM_PTX)

PLATFORM=l4t-cross-r32-5-0

BUILD_LINUX=ON
BUILD_L4T=ON
BUILD_L4T3250=ON
