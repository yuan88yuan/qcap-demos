include(CMakeParseArguments)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/_objs/${QCAP_PLATFORM}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/_objs/${QCAP_PLATFORM}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/_objs/${QCAP_PLATFORM}/lib")

function(qcap_configure_target target)
  # Make explicitly passes -fPIC for C/C++ objects. Disable CMake's automatic
  # executable -fPIE choice and apply the same flag explicitly.
  set_target_properties(${target} PROPERTIES POSITION_INDEPENDENT_CODE OFF)
  target_compile_options(${target} PRIVATE -O3
    "$<$<COMPILE_LANGUAGE:CXX>:-fPIC>"
    "$<$<COMPILE_LANGUAGE:CXX>:-fpermissive>"
    "$<$<COMPILE_LANGUAGE:C>:-fPIC>")
  foreach(flag IN LISTS QCAP_CXX_FLAGS)
    target_compile_options(${target} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:${flag}>")
  endforeach()
  foreach(flag IN LISTS QCAP_C_FLAGS)
    target_compile_options(${target} PRIVATE "$<$<COMPILE_LANGUAGE:C>:${flag}>")
  endforeach()
  target_compile_definitions(${target} PRIVATE
    BUILD_SYS=2
    OS_LINUX
    "GIT_COMMIT_HASH=\"${GIT_COMMIT_HASH}\""
    "GIT_BRANCH_NAME=\"${GIT_BRANCH_NAME}\""
    ${QCAP_PLATFORM_DEFINITIONS})
  target_link_options(${target} PRIVATE ${QCAP_LINK_FLAGS})
  target_link_libraries(${target} PUBLIC qcap::base)
endfunction()

function(qcap_link_modules target)
  foreach(module IN LISTS ARGN)
    string(TOLOWER "${module}" module_lower)
    if(TARGET qcap::${module_lower})
      target_link_libraries(${target} PRIVATE qcap::${module_lower})
    endif()
  endforeach()
endfunction()

function(qcap_add_executable target)
  cmake_parse_arguments(QCAP "TEST" "" "SOURCES;DEPS" ${ARGN})
  if(QCAP_TEST)
    add_executable(${target} EXCLUDE_FROM_ALL ${QCAP_SOURCES})
  else()
    add_executable(${target} ${QCAP_SOURCES})
  endif()
  qcap_configure_target(${target})
  qcap_link_modules(${target} ${QCAP_DEPS})
endfunction()

if(BUILD_WITH_ZZLAB)
  add_library(zzlab STATIC
    "${PROJECT_SOURCE_DIR}/src/zzlab/ZzClock.cpp"
    "${PROJECT_SOURCE_DIR}/src/zzlab/ZzLog.cpp"
    "${PROJECT_SOURCE_DIR}/src/zzlab/ZzModules.cpp"
    "${PROJECT_SOURCE_DIR}/src/zzlab/ZzStats.cpp"
    "${PROJECT_SOURCE_DIR}/src/zzlab/ZzUtils.cpp"
    "${PROJECT_SOURCE_DIR}/src/zzlab/ZzUtils_string.cpp"
    "${PROJECT_SOURCE_DIR}/src/zzlab/ZzUtils_env.cpp"
    "${PROJECT_SOURCE_DIR}/src/zzlab/ZzUtils_memory.cpp")
  add_library(qcap::zzlab ALIAS zzlab)
  set_target_properties(zzlab PROPERTIES OUTPUT_NAME zzlab)
  qcap_configure_target(zzlab)
  target_include_directories(zzlab PUBLIC "${PROJECT_SOURCE_DIR}/src/zzlab")
  target_compile_definitions(zzlab PUBLIC BUILD_WITH_ZZLAB=1)
endif()

# Utilities (the default Make 'all' build).
qcap_add_executable(qdemo
  SOURCES "${PROJECT_SOURCE_DIR}/src/utils/qdemo.cpp"
  DEPS QCAP ZZLAB)
qcap_add_executable(sc6f0-dante-box
  SOURCES "${PROJECT_SOURCE_DIR}/src/utils/sc6f0-dante-box.cpp"
  DEPS QCAP ZZLAB)
qcap_add_executable(zdev-ticks-stat
  SOURCES "${PROJECT_SOURCE_DIR}/src/utils/zdev-ticks-stat.cpp"
  DEPS)

# Tests/demos registered by src/tests/tests.mk.
set(TEST_TARGETS)
macro(qcap_add_test target source)
  qcap_add_executable(${target} TEST SOURCES "${PROJECT_SOURCE_DIR}/src/tests/${source}" DEPS ${ARGN})
  list(APPEND TEST_TARGETS ${target})
endmacro()

set(TEST_AVCAP_SOURCES "${PROJECT_SOURCE_DIR}/src/tests/test-avcap.cpp")
if(QCAP_ENABLE_CUDA_SOURCE)
  list(APPEND TEST_AVCAP_SOURCES "${PROJECT_SOURCE_DIR}/src/tests/test-avcap.cu")
endif()
qcap_add_executable(test-avcap TEST SOURCES ${TEST_AVCAP_SOURCES} DEPS QCAP ZZLAB)
list(APPEND TEST_TARGETS test-avcap)
if(BUILD_SC6N0)
  qcap_link_modules(test-avcap NVBUF CUDA NPP)
endif()

qcap_add_test(test-hsb-vsrc test-hsb-vsrc.cpp ZZLAB QCAP CUDA FMT IBVERBS)
qcap_add_test(test-deint test-deint.cpp ZZLAB QCAP)
qcap_add_test(test-lic test-lic.cpp ZZLAB QCAP QCAP2_LIC)
qcap_add_test(bsci-demo bsci-demo.cpp ZZLAB QCAP NVBUF CUDA NPP)
qcap_add_test(test-zznvenc2 test-zznvenc2.cpp ZZLAB QCAP)
qcap_add_test(test-ipx test-ipx.cpp ZZLAB QCAP CUDA IPX NVBUF)
qcap_add_test(kylin-demo kylin-demo.cpp ZZLAB QCAP)
qcap_add_test(sc6g0-aenc sc6g0-aenc.cpp ZZLAB QCAP NVT_HDAL)
qcap_add_test(test-aenc test-aenc.cpp ZZLAB QCAP NVT_HDAL)
qcap_add_test(test-freetype test-freetype.cpp ZZLAB QCAP FONTCONFIG FREETYPE HARFBUZZ)
qcap_add_test(test-graphics test-graphics.cpp ZZLAB QCAP)
if(BUILD_SC6G0)
  qcap_link_modules(test-graphics NVT_HDAL)
endif()
qcap_add_test(sc6f0-dante-demo sc6f0-dante-demo.cpp ZZLAB QCAP ALLEGRO2 DAUSERVICE DRM)
qcap_add_test(sc6f0-dante-tx sc6f0-dante-tx.cpp ZZLAB QCAP ALLEGRO2 DAUSERVICE DRM)
qcap_add_test(sc6f0-dante-rx sc6f0-dante-rx.cpp ZZLAB QCAP ALSA ALLEGRO2 DAUSERVICE DRM)
qcap_add_test(coe-demo coe-demo.cpp ZZLAB QCAP CUDA CUDA_DRIVER SIPL)
qcap_add_test(test-rcbuf test-rcbuf.cpp ZZLAB QCAP)
qcap_add_test(roce-dmabuf-test roce-dmabuf-test.cpp ZZLAB QCAP CUDA CUDA_DRIVER)
qcap_add_test(test-demuxer test-demuxer.cpp ZZLAB QCAP ALLEGRO2)
qcap_add_test(sc6f0-sdi sc6f0-sdi.cpp ZZLAB QCAP)
qcap_add_test(test-vdec-bench test-vdec-bench.cpp ZZLAB QCAP)

# Jetson Multimedia API classes are external sources/objects in the Make build.
# Keep the executable represented, but only add the test to the aggregate when
# the platform supplies the required API tree.
set(QCAP_NVDEC_CLASS_DIR "/usr/src/jetson_multimedia_api/samples/common/classes"
    CACHE PATH "Jetson Multimedia API common classes")
set(TEST_SWDEC_TARGET test-swdec-bench)
qcap_add_executable(${TEST_SWDEC_TARGET} TEST
  SOURCES "${PROJECT_SOURCE_DIR}/src/tests/test-swdec-bench.cpp"
  DEPS ZZLAB QCAP)
if(EXISTS "${QCAP_NVDEC_CLASS_DIR}/NvVideoDecoder.cpp")
  set(NVDEC_CLASS_SOURCES
    NvVideoDecoder.cpp NvV4l2Element.cpp NvV4l2ElementPlane.cpp NvElement.cpp
    NvElementProfiler.cpp NvBuffer.cpp NvLogging.cpp NvUtils.cpp NvBufSurface.cpp)
  foreach(source IN LISTS NVDEC_CLASS_SOURCES)
    target_sources(${TEST_SWDEC_TARGET} PRIVATE "${QCAP_NVDEC_CLASS_DIR}/${source}")
  endforeach()
  target_include_directories(${TEST_SWDEC_TARGET} PRIVATE
    /usr/src/jetson_multimedia_api/include
    /usr/src/jetson_multimedia_api/include/libjpeg-8b
    "${QCAP_NVDEC_CLASS_DIR}")
  target_link_directories(${TEST_SWDEC_TARGET} PRIVATE /usr/lib/aarch64-linux-gnu/nvidia)
  target_link_libraries(${TEST_SWDEC_TARGET} PRIVATE
    -lnvv4l2 -lnvbufsurface -lnvbufsurftransform)
  list(APPEND TEST_TARGETS ${TEST_SWDEC_TARGET})
else()
  message(STATUS "Skipping ${TEST_SWDEC_TARGET} from the tests aggregate: ${QCAP_NVDEC_CLASS_DIR} is unavailable")
endif()

qcap_add_test(sc6n0-demuxer sc6n0-demuxer.cpp ZZLAB QCAP)

add_custom_target(utils DEPENDS qdemo sc6f0-dante-box zdev-ticks-stat)
add_custom_target(tests DEPENDS ${TEST_TARGETS})

# Make-compatible housekeeping targets. CMake's built-in 'clean' remains the
# authoritative generated-file cleanup target.
add_custom_target(clean-bin
  COMMAND "${CMAKE_COMMAND}" -E rm -rf "${PROJECT_SOURCE_DIR}/_objs/${QCAP_PLATFORM}/bin"
  COMMENT "Removing qcap-demos binaries")
add_custom_target(clean-lib
  COMMAND "${CMAKE_COMMAND}" -E rm -rf "${PROJECT_SOURCE_DIR}/_objs/${QCAP_PLATFORM}/lib"
  COMMENT "Removing qcap-demos libraries")
# An install(CODE) declaration creates CMake's reserved `install` target while
# retaining the current GNU Make no-op behavior.
install(CODE "message(STATUS \"No install rules are defined (matching GNU Make).\")")
add_custom_target(package
  COMMAND "${CMAKE_COMMAND}" -E echo "No package rules are defined - matching GNU Make.")
add_dependencies(package utils)
