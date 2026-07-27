# Toolchain and effective settings from xlnk2_arm64.mk plus /opt/qcap-dev-init.
# This file is loaded both as CMAKE_TOOLCHAIN_FILE and as the project platform
# description, so all settings are intentionally idempotent.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

if(NOT SDKTARGETSYSROOT AND DEFINED ENV{SDKTARGETSYSROOT})
  set(SDKTARGETSYSROOT "$ENV{SDKTARGETSYSROOT}" CACHE PATH "Target sysroot" FORCE)
endif()
if(NOT SDKTARGETSYSROOT)
  message(FATAL_ERROR "SDKTARGETSYSROOT is not set. Source /opt/qcap-dev-init in the xlnk2 build image.")
endif()

set(CMAKE_SYSROOT "${SDKTARGETSYSROOT}" CACHE PATH "Target sysroot" FORCE)
set(CMAKE_C_COMPILER aarch64-xilinx-linux-gcc CACHE FILEPATH "Target C compiler")
set(CMAKE_CXX_COMPILER aarch64-xilinx-linux-g++ CACHE FILEPATH "Target C++ compiler")
set(CMAKE_AR aarch64-xilinx-linux-ar CACHE FILEPATH "Target archiver")
set(CMAKE_RANLIB aarch64-xilinx-linux-ranlib CACHE FILEPATH "Target ranlib")
set(CMAKE_STRIP aarch64-xilinx-linux-strip CACHE FILEPATH "Target strip")

set(QCAP_PLATFORM_NAME "xlnk2_arm64" CACHE INTERNAL "Selected qcap platform")
set(LINUX_GNU_LIB "lib" CACHE STRING "Architecture-specific library directory")

# Flags injected by the xlnk2 qcap-dev toolchain environment.
set(QCAP_TOOLCHAIN_FLAGS
  "-mcpu=cortex-a72.cortex-a53 -march=armv8-a+crc -fstack-protector-strong -O2 -D_FORTIFY_SOURCE=2 -Wformat -Wformat-security -Werror=format-security"
  CACHE STRING "xlnk2 toolchain flags")
set(CMAKE_C_FLAGS_INIT "${QCAP_TOOLCHAIN_FLAGS} -O2 -pipe -g -feliminate-unused-debug-types")
set(CMAKE_CXX_FLAGS_INIT "${QCAP_TOOLCHAIN_FLAGS} -O2 -pipe -g -feliminate-unused-debug-types")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${QCAP_TOOLCHAIN_FLAGS}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${QCAP_TOOLCHAIN_FLAGS}")

set(QCAP_CXX_FLAGS "-Wno-nonnull;-Wno-deprecated-declarations" CACHE STRING "Platform C++ flags")
set(QCAP_C_FLAGS "-Wno-nonnull;-Wno-deprecated-declarations" CACHE STRING "Platform C flags")
set(QCAP_LINK_FLAGS "-Wl,-O1;-Wl,--hash-style=gnu;-Wl,--as-needed;-Wl,-z,relro,-z,now" CACHE STRING "Platform linker flags")

set(BUILD_LINUX ON CACHE BOOL "Enable Linux platform definitions" FORCE)
set(BUILD_XLNK2_ARM64 ON CACHE BOOL "Enable xlnk2 arm64 definitions" FORCE)
set(WITH_NEON ON CACHE BOOL "Enable NEON definitions" FORCE)

# Match xlnk2_arm64.mk feature defaults.
foreach(module IN ITEMS ZZLAB QCAP ALSA FREETYPE HARFBUZZ X264 OPENSSL ALLEGRO2 DRM DAUSERVICE)
  set(BUILD_WITH_${module} ON CACHE BOOL "Enable ${module}" FORCE)
endforeach()
set(BUILD_WITH_QCAP2_LIC OFF CACHE BOOL "Enable QCAP2 licensing" FORCE)
set(BUILD_WITH_FONTCONFIG OFF CACHE BOOL "Enable Fontconfig" FORCE)
set(QCAP_ENABLE_CUDA_SOURCE OFF CACHE BOOL "Compile CUDA sources" FORCE)
