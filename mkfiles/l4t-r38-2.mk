CXX=g++
CC=gcc
STRIP=strip
AR=ar
RANLIB=ranlib
CXXFLAGS=-Wno-write-strings -ftree-vectorize -ftree-vectorizer-verbose=5 -std=c++17
CFLAGS=-Wno-write-strings -ftree-vectorize -ftree-vectorizer-verbose=5
LDFLAGS=-Wl,-Bsymbolic
LINUX_GNU_LIB=lib/aarch64-linux-gnu

NVCC=/usr/local/cuda/bin/nvcc

# CUDA code generation flags
GENCODE_FLAGS=-gencode arch=compute_110,code=sm_110

PLATFORM=l4t-r38-2

BUILD_LINUX=ON
BUILD_L4T=ON
BUILD_L4T3820=ON
