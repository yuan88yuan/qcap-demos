# Effective settings from mkfiles/ubuntu2004_x64.mk.
set(QCAP_PLATFORM_NAME "ubuntu2004_x64" CACHE INTERNAL "Selected qcap platform")
set(LINUX_GNU_LIB "lib/x86_64-linux-gnu" CACHE STRING "Architecture-specific library directory")

set(QCAP_CXX_FLAGS "-std=c++11;-msse4" CACHE STRING "Platform C++ flags")
set(QCAP_C_FLAGS "-msse4" CACHE STRING "Platform C flags")
set(QCAP_LINK_FLAGS "-Wl,-O1;-Wl,--hash-style=gnu;-Wl,--as-needed" CACHE STRING "Platform linker flags")
set(QCAP_CUDA_COMPILER "/usr/local/cuda/bin/nvcc" CACHE FILEPATH "Platform CUDA compiler")

set(BUILD_LINUX ON CACHE BOOL "Enable Linux platform definitions" FORCE)
set(BUILD_UBUNTU2004_X64 ON CACHE BOOL "Enable Ubuntu 20.04 x64 definitions" FORCE)
