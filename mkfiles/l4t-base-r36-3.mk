CXX=g++
CC=gcc
STRIP=strip
AR=ar
RANLIB=ranlib
CXXFLAGS=-std=c++11
CFLAGS=
LDFLAGS=-Wl,-O1 -Wl,--hash-style=gnu -Wl,--as-needed
LINUX_GNU_LIB=lib/aarch64-linux-gnu

NVCC=/usr/local/cuda/bin/nvcc

# CUDA code generation flags
GENCODE_SM53=-gencode arch=compute_53,code=sm_53
GENCODE_SM62=-gencode arch=compute_62,code=sm_62
GENCODE_SM72=-gencode arch=compute_72,code=sm_72
GENCODE_SM87=-gencode arch=compute_87,code=sm_87
GENCODE_SM_PTX=-gencode arch=compute_72,code=compute_72
GENCODE_FLAGS=$(GENCODE_SM53) $(GENCODE_SM62) $(GENCODE_SM72) $(GENCODE_SM87) $(GENCODE_SM_PTX)

PLATFORM=l4t-base-r36-3

BUILD_LINUX=ON
BUILD_L4T=ON
BUILD_L4T3630=ON
