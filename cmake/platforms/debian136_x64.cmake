# Effective settings from mkfiles/debian136_x64.mk.
set(QCAP_PLATFORM_NAME "debian136_x64" CACHE INTERNAL "Selected qcap platform")
set(LINUX_GNU_LIB "lib/x86_64-linux-gnu" CACHE STRING "Architecture-specific library directory")

set(QCAP_CXX_FLAGS "-std=c++11" CACHE STRING "Platform C++ flags")
set(QCAP_C_FLAGS "" CACHE STRING "Platform C flags")
set(QCAP_LINK_FLAGS "-Wl,-O1;-Wl,--hash-style=gnu;-Wl,--as-needed" CACHE STRING "Platform linker flags")

set(BUILD_LINUX ON CACHE BOOL "Enable Linux platform definitions" FORCE)
set(BUILD_DEBIAN136_X64 ON CACHE BOOL "Enable Debian 13.6 x64 definitions" FORCE)

# The -base image has no CUDA toolkit; do not require nvcc at configure time.
set(QCAP_ENABLE_CUDA_SOURCE OFF CACHE BOOL "Compile CUDA sources such as test-avcap.cu" FORCE)
