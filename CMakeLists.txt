cmake_minimum_required(VERSION 3.16)

if(POLICY CMP0104)
  # The Make build does not add a default -gencode list for ubuntu2004_x64.
  cmake_policy(SET CMP0104 OLD)
endif()

project(qcap-demos LANGUAGES C CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# GNU Make uses -rpath-link, not an embedded runtime RPATH. Keep that behavior.
set(CMAKE_SKIP_RPATH TRUE)

set(QCAP_PLATFORM "ubuntu2004_x64" CACHE STRING "Target qcap platform")
set(QCAP_PLATFORM_FILE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/platforms/${QCAP_PLATFORM}.cmake")
if(NOT EXISTS "${QCAP_PLATFORM_FILE}")
  message(FATAL_ERROR "Unsupported QCAP_PLATFORM='${QCAP_PLATFORM}'. Expected a platform file at ${QCAP_PLATFORM_FILE}.")
endif()
include("${QCAP_PLATFORM_FILE}")

if(DEFINED ENV{QCAP_HOME} AND NOT DEFINED QCAP_HOME)
  set(QCAP_HOME "$ENV{QCAP_HOME}" CACHE PATH "qcap installation/build root")
else()
  set(QCAP_HOME "${CMAKE_CURRENT_SOURCE_DIR}/../qcap-dev/qcap/build-qcap/_objs/${QCAP_PLATFORM}"
      CACHE PATH "qcap installation/build root")
endif()

set(SDKTARGETSYSROOT "" CACHE PATH "Target sysroot for cross builds")
set(DOCKER_HOME ".." CACHE PATH "Docker/project dependency root")
set(NOVATEK_SDK "" CACHE PATH "Novatek SDK root")

# These defaults match the effective ubuntu2004_x64 Make configuration.
option(BUILD_WITH_ZZLAB "Build the zzlab utility library" ON)
option(BUILD_WITH_QCAP "Use the prebuilt qcap library" ON)
option(BUILD_WITH_QCAP2_LIC "Use the prebuilt qcap2 licensing library" ON)
option(BUILD_WITH_FONTCONFIG "Enable Fontconfig" ON)
option(BUILD_WITH_FREETYPE "Enable FreeType" ON)
option(BUILD_WITH_HARFBUZZ "Enable HarfBuzz" ON)

# Modules that are unconditionally declared by the Make module catalog.
option(BUILD_WITH_BOOST "Enable Boost" ON)
option(BUILD_WITH_FFMPEG "Enable FFmpeg" ON)
option(BUILD_WITH_ZLIB "Enable zlib" ON)
option(BUILD_WITH_ICONV "Enable iconv" ON)

# Optional modules. Keep these OFF unless the selected platform/application needs them.
set(QCAP_OPTIONAL_MODULES
  ALSA ALLEGRO2 CUDA CUDA_DRIVER DAUSERVICE DRM FMT GSTREAMER IBVERBS IPP
  NPP NVBUF NVJPEG NVT_HDAL ONETBB OPENSSL SIPL SDL SPEEXDSP UDEV VAAPI X11
  X264 YUV FONTCONFIG FREETYPE HARFBUZZ
)
foreach(module IN LISTS QCAP_OPTIONAL_MODULES)
  if(NOT DEFINED BUILD_WITH_${module})
    option(BUILD_WITH_${module} "Enable ${module}" OFF)
  endif()
endforeach()

# Platform and application feature switches used by mkfiles/flags.mk.
foreach(flag IN ITEMS
    BUILD_LINUX BUILD_L4T BUILD_HISIV BUILD_DESKTOP
    BUILD_UBUNTU2004_X64 BUILD_XLNK2_ARM64 BUILD_CENTOS76_X64 BUILD_DEBIAN105_X64
    BUILD_UBUNTU1604_X64 BUILD_UBUNTU1804_X64 BUILD_UBUNTU2404_X64
    BUILD_SC6F0 BUILD_SC6E0 BUILD_SC6N0 BUILD_SC6T0 BUILD_SC6G0
    WITH_NEON WITH_SSE)
  if(NOT DEFINED ${flag})
    option(${flag} "Enable ${flag}" OFF)
  endif()
endforeach()

# test-avcap.cu is part of the Make target even when BUILD_WITH_CUDA is OFF.
option(QCAP_ENABLE_CUDA_SOURCE "Compile CUDA sources such as test-avcap.cu" ON)
if(QCAP_ENABLE_CUDA_SOURCE)
  if(NOT CMAKE_CUDA_COMPILER AND EXISTS "/usr/local/cuda/bin/nvcc")
    set(CMAKE_CUDA_COMPILER "/usr/local/cuda/bin/nvcc" CACHE FILEPATH "CUDA compiler")
  endif()
  enable_language(CUDA)
  set(CMAKE_CUDA_STANDARD 11)
  set(CMAKE_CUDA_STANDARD_REQUIRED ON)
endif()

include(cmake/QcapPlatform.cmake)
include(cmake/QcapDependencies.cmake)
include(cmake/QcapTargets.cmake)
