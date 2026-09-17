# Native ARM64 Jetson Thor target for Jetson Linux 39.2.1 / JetPack 7.2.1.
# The r39.x family currently uses the compatible r38 multimedia build contract
# until a distinct r39 platform fragment is introduced.

CXX=g++
CC=gcc
STRIP=strip
AR=ar
RANLIB=ranlib
NOWARN=-Wno-deprecated-declarations -Wno-format-truncation
CXXNOWARN=$(NOWARN)
CNOWARN=$(NOWARN) -Wno-discarded-qualifiers
CXXFLAGS=-Wno-write-strings -ftree-vectorize -ftree-vectorizer-verbose=5 -std=c++17 $(CXXNOWARN)
CFLAGS=-Wno-write-strings -ftree-vectorize -ftree-vectorizer-verbose=5 $(CNOWARN)
LDFLAGS=-Wl,-Bsymbolic
LINUX_GNU_LIB=lib/aarch64-linux-gnu

NVCC=/usr/local/cuda/bin/nvcc

# CUDA code generation flags
GENCODE_FLAGS=-gencode arch=compute_110,code=sm_110

PLATFORM=l4t-r39-2

BUILD_LINUX=ON
BUILD_L4T=ON
BUILD_L4T3820=ON
