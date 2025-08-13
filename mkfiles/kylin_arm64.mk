CXX=g++
CC=gcc
STRIP=strip
AR=ar
RANLIB=ranlib
CXXFLAGS=-Wno-write-strings -ftree-vectorize -ftree-vectorizer-verbose=5 -std=c++11
CFLAGS=-Wno-write-strings -ftree-vectorize -ftree-vectorizer-verbose=5
LDFLAGS=-Wl,-Bsymbolic
LINUX_GNU_LIB=lib64

PLATFORM=kylin_arm64

BUILD_LINUX=ON
BUILD_KYLIN_ARM64=ON
