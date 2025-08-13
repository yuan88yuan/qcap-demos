CXX=g++
CC=gcc
STRIP=strip
AR=ar
RANLIB=ranlib
CXXFLAGS=-std=c++11 -msse4
CFLAGS=-msse4
LDFLAGS=-Wl,-O1 -Wl,--hash-style=gnu -Wl,--as-needed
LINUX_GNU_LIB=lib/x86_64-linux-gnu

PLATFORM=ubuntu1604_x64

BUILD_LINUX=ON
BUILD_UBUNTU1604_X64=ON
