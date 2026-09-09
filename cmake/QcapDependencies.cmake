include(CMakeParseArguments)

if(NOT EXISTS "${QCAP_HOME}/include")
  message(FATAL_ERROR "QCAP_HOME is invalid: missing ${QCAP_HOME}/include")
endif()
if(NOT EXISTS "${QCAP_HOME}/lib")
  message(FATAL_ERROR "QCAP_HOME is invalid: missing ${QCAP_HOME}/lib")
endif()
# Third-party files are part of the selected qcap build image. They are not
# configurable project inputs; the image layout is platform-specific.
set(QCAP_IMAGE_THIRDPARTY_DIR "/opt/qcap/qcap-3rdparty/${QCAP_PLATFORM}")
if(NOT EXISTS "${QCAP_IMAGE_THIRDPARTY_DIR}/include")
  message(FATAL_ERROR "Build image is missing third-party headers: ${QCAP_IMAGE_THIRDPARTY_DIR}/include")
endif()
if(NOT EXISTS "${QCAP_IMAGE_THIRDPARTY_DIR}/lib")
  message(FATAL_ERROR "Build image is missing third-party libraries: ${QCAP_IMAGE_THIRDPARTY_DIR}/lib")
endif()

# Common BASE module from mkfiles/base.mk.
add_library(qcap_demos_base INTERFACE)
add_library(qcap::base ALIAS qcap_demos_base)
# Repo-local 3rdparty dir mirrors mkfiles/base.mk's -I${S}/../3rdparty/include
# and -L${S}/../3rdparty/lib (platform-specific deps such as dauservice).
target_include_directories(qcap_demos_base INTERFACE
  "${PROJECT_SOURCE_DIR}/include"
  "${PROJECT_SOURCE_DIR}/3rdparty/include"
  "${QCAP_IMAGE_THIRDPARTY_DIR}/include"
)
target_link_directories(qcap_demos_base INTERFACE
  "${QCAP_IMAGE_THIRDPARTY_DIR}/lib"
  "${PROJECT_SOURCE_DIR}/3rdparty/lib"
  "${CMAKE_BINARY_DIR}/lib"
)
target_link_options(qcap_demos_base INTERFACE
  "-Wl,-rpath-link,${QCAP_IMAGE_THIRDPARTY_DIR}/lib"
  "-Wl,-rpath-link,${PROJECT_SOURCE_DIR}/3rdparty/lib"
  "-Wl,-rpath-link,${CMAKE_BINARY_DIR}/lib"
  -pthread)
target_link_libraries(qcap_demos_base INTERFACE rt dl)

function(qcap_define_module module)
  string(TOLOWER "${module}" module_lower)
  if(BUILD_WITH_${module})
    add_library(qcap_demos_${module_lower} INTERFACE)
    add_library(qcap::${module_lower} ALIAS qcap_demos_${module_lower})
    target_compile_definitions(qcap_demos_${module_lower} INTERFACE
      "BUILD_WITH_${module}=1")
  endif()
endfunction()

function(qcap_module_includes module)
  string(TOLOWER "${module}" module_lower)
  if(TARGET qcap::${module_lower})
    target_include_directories(qcap_demos_${module_lower} INTERFACE ${ARGN})
  endif()
endfunction()

function(qcap_module_links module)
  string(TOLOWER "${module}" module_lower)
  if(TARGET qcap::${module_lower})
    target_link_libraries(qcap_demos_${module_lower} INTERFACE ${ARGN})
  endif()
endfunction()

function(qcap_module_static_links module)
  string(TOLOWER "${module}" module_lower)
  if(TARGET qcap::${module_lower})
    target_link_libraries(qcap_demos_${module_lower} INTERFACE
      -Wl,-Bstatic ${ARGN} -Wl,-Bdynamic)
  endif()
endfunction()

function(qcap_module_link_options module)
  string(TOLOWER "${module}" module_lower)
  if(TARGET qcap::${module_lower})
    target_link_options(qcap_demos_${module_lower} INTERFACE ${ARGN})
  endif()
endfunction()

# qcap is prebuilt. It is deliberately imported by absolute path rather than
# represented as -lqcap, preventing an accidental host-library selection.
if(BUILD_WITH_QCAP OR BUILD_WITH_QCAP2_LIC)
  find_path(QCAP_INCLUDE_DIR NAMES qcap2.h
    PATHS "${QCAP_HOME}/include" NO_DEFAULT_PATH)
endif()
if(BUILD_WITH_QCAP)
  unset(QCAP_LIBRARY CACHE)
  find_library(QCAP_LIBRARY NAMES qcap
    PATHS "${QCAP_HOME}/lib" NO_DEFAULT_PATH)
  if(NOT QCAP_INCLUDE_DIR OR NOT QCAP_LIBRARY)
    message(FATAL_ERROR
      "Could not locate qcap headers/library under QCAP_HOME=${QCAP_HOME} "
      "(expected qcap2.h and libqcap.*)")
  endif()
  # This interface target is backed by the validated prebuilt library. Use
  # -L/-l at link time to preserve Make's libqcap.so NEEDED name, while the
  # qcap-only link directory prevents accidental host-library selection.
  add_library(qcap_qcap INTERFACE)
  add_library(qcap::qcap ALIAS qcap_qcap)
  target_include_directories(qcap_qcap INTERFACE "${QCAP_INCLUDE_DIR}")
  target_compile_definitions(qcap_qcap INTERFACE BUILD_WITH_QCAP=1)
  target_link_directories(qcap_qcap INTERFACE "${QCAP_HOME}/lib")
  target_link_options(qcap_qcap INTERFACE "-Wl,-rpath-link,${QCAP_HOME}/lib")
  target_link_libraries(qcap_qcap INTERFACE qcap)
endif()

unset(QCAP2_LIC_LIBRARY CACHE)
find_library(QCAP2_LIC_LIBRARY NAMES qcap2_lic
  PATHS "${QCAP_HOME}/lib" NO_DEFAULT_PATH)
if(BUILD_WITH_QCAP2_LIC)
  if(NOT QCAP_INCLUDE_DIR)
    message(FATAL_ERROR
      "BUILD_WITH_QCAP2_LIC is ON but qcap headers were not found under ${QCAP_HOME}/include")
  endif()
  if(NOT QCAP2_LIC_LIBRARY)
    message(FATAL_ERROR
      "BUILD_WITH_QCAP2_LIC is ON but libqcap2_lic.* was not found under ${QCAP_HOME}/lib")
  endif()
  add_library(qcap_qcap2_lic INTERFACE)
  add_library(qcap::qcap2_lic ALIAS qcap_qcap2_lic)
  target_include_directories(qcap_qcap2_lic INTERFACE "${QCAP_INCLUDE_DIR}")
  target_compile_definitions(qcap_qcap2_lic INTERFACE BUILD_QCAP2=1 BUILD_WITH_QCAP2_LIC=1)
  target_link_directories(qcap_qcap2_lic INTERFACE "${QCAP_HOME}/lib")
  target_link_options(qcap_qcap2_lic INTERFACE "-Wl,-rpath-link,${QCAP_HOME}/lib")
  target_link_libraries(qcap_qcap2_lic INTERFACE qcap2_lic)
endif()

# Declare the module catalog. Targets are created only when their corresponding
# BUILD_WITH_* option is ON, matching add_mod behavior in the Make files.
foreach(module IN ITEMS
    BOOST FFMPEG ZLIB ICONV FREETYPE HARFBUZZ FONTCONFIG GSTREAMER CUDA
    CUDA_DRIVER NPP NVBUF NVJPEG OPENSSL SDL ALSA X11 VAAPI UDEV YUV IPP
    ONETBB DRM SPEEXDSP FMT IBVERBS IPX NVT_HDAL ALLEGRO2 SIPL DAUSERVICE ZSTREAMER)
  qcap_define_module(${module})
endforeach()

if(NOT ZSTREAMER_ROOT)
  if(DEFINED ENV{ZSTREAMER_ROOT})
    set(ZSTREAMER_ROOT "$ENV{ZSTREAMER_ROOT}" CACHE PATH "zstreamer root")
  else()
    set(ZSTREAMER_ROOT "${PROJECT_SOURCE_DIR}/../zstreamer" CACHE PATH "zstreamer root")
  endif()
endif()

if(TARGET qcap::zstreamer)
  qcap_module_includes(ZSTREAMER
    "${ZSTREAMER_ROOT}/include"
    "${ZSTREAMER_ROOT}/include/zstreamer/elements"
  )
  set(ZSTREAMER_LIB_DIR "${ZSTREAMER_ROOT}/build-xlnk2_arm64/src")
  if(NOT EXISTS "${ZSTREAMER_LIB_DIR}/libzstreamer.so"
     AND NOT EXISTS "${ZSTREAMER_LIB_DIR}/libzstreamer.a")
    set(ZSTREAMER_LIB_DIR "${PROJECT_SOURCE_DIR}/3rdparty/lib")
  endif()
  target_link_directories(qcap_demos_zstreamer INTERFACE "${ZSTREAMER_LIB_DIR}")
  qcap_module_links(ZSTREAMER
    -lzstreamer-elements -lzstreamer
    -ljson-c -lpthread -lm -lrt -ldl)
endif()

qcap_module_static_links(BOOST
  -lboost_system -lboost_thread -lboost_atomic -lboost_chrono
  -lboost_context -lboost_filesystem -lboost_program_options -lboost_coroutine)
qcap_module_static_links(FFMPEG
  -lavformat -lavfilter -lavcodec -lavutil -lswresample -lswscale -lpostproc -lfdk-aac)
if(BUILD_L4T)
  qcap_module_links(FFMPEG -lvdpau)
endif()
qcap_module_static_links(ZLIB -lz)
qcap_module_static_links(ICONV -liconv)
qcap_module_static_links(FREETYPE -lfreetype)
qcap_module_static_links(HARFBUZZ -lharfbuzz -lfreetype)
qcap_module_links(FONTCONFIG -lfontconfig)
qcap_module_links(GSTREAMER
  -lgstreamer-1.0 -lgobject-2.0 -lglib-2.0 -lgstallocators-1.0
  -lgstvideo-1.0 -lgstapp-1.0)
qcap_module_links(CUDA -lcudart)
qcap_module_links(CUDA_DRIVER -lcuda)
qcap_module_links(NPP
  -lnppc -lnppial -lnppicc -lnppidei -lnppif -lnppig -lnppim
  -lnppist -lnppisu -lnppitc -lnpps)
qcap_module_static_links(OPENSSL -lssl -lcrypto)
qcap_module_static_links(SDL -lSDL2)
qcap_module_static_links(X264 -lx264)
if(QCAP_PLATFORM STREQUAL "novatek_arm64")
  qcap_module_static_links(ALSA -lasound)
else()
  qcap_module_links(ALSA -lasound)
endif()
qcap_module_links(X11 -lX11 -lXv)
qcap_module_links(VAAPI -lva -lva-drm -lva-x11)
qcap_module_links(UDEV -ludev)
qcap_module_static_links(YUV -lyuv)
qcap_module_static_links(IPP -lippcore -lippi -lippcc -lippvc -lipps -lirc -lsvml)
qcap_module_links(ONETBB -ltbb)
qcap_module_links(DRM -ldrm)
qcap_module_links(SPEEXDSP -lspeexdsp)
qcap_module_links(FMT -lfmt)
qcap_module_links(IBVERBS -libverbs)
qcap_module_links(IPX -lIpxGpuCodec)
qcap_module_links(NVJPEG -lnvjpeg)
qcap_module_links(NVBUF -lnvbufsurface -lv4l2)
qcap_module_links(NVT_HDAL -lhdal -lvendor_media -lbrnvt)
qcap_module_links(ALLEGRO2 -lallegro_decode -lallegro_encode)
qcap_module_links(SIPL -lnvscibuf -lnvsipl -lnvsipl_query)
qcap_module_static_links(DAUSERVICE -ldauservice)
qcap_module_links(DAUSERVICE -ldbus-1)

qcap_module_includes(FREETYPE "${QCAP_IMAGE_THIRDPARTY_DIR}/include/freetype2")
qcap_module_includes(HARFBUZZ "${QCAP_IMAGE_THIRDPARTY_DIR}/include/harfbuzz")
qcap_module_includes(GSTREAMER
  "${SDKTARGETSYSROOT}/usr/include"
  "${SDKTARGETSYSROOT}/usr/include/glib-2.0"
  "${SDKTARGETSYSROOT}/usr/include/gstreamer-1.0"
  "${SDKTARGETSYSROOT}/usr/${LINUX_GNU_LIB}/glib-2.0/include"
  "${SDKTARGETSYSROOT}/usr/${LINUX_GNU_LIB}/gstreamer-1.0/include")
qcap_module_includes(CUDA "${SDKTARGETSYSROOT}/usr/local/cuda/include")
qcap_module_includes(CUDA_DRIVER "${SDKTARGETSYSROOT}/usr/local/cuda/include")
qcap_module_includes(NVBUF "${SDKTARGETSYSROOT}/usr/src/jetson_multimedia_api/include")
qcap_module_includes(DRM "${SDKTARGETSYSROOT}/usr/include/drm")
qcap_module_includes(ALLEGRO2
  "${QCAP_IMAGE_THIRDPARTY_DIR}/include/allegro"
  "${QCAP_IMAGE_THIRDPARTY_DIR}/include/allegro-omx-il")
qcap_module_includes(SIPL
  "${SDKTARGETSYSROOT}/usr/src/jetson_sipl_api/sipl/include/nvsci"
  "${SDKTARGETSYSROOT}/usr/src/jetson_sipl_api/sipl/include/nvmedia"
  "${SDKTARGETSYSROOT}/usr/src/jetson_sipl_api/sipl/include"
  "${SDKTARGETSYSROOT}/usr/src/jetson_sipl_api/sipl/include/query/include")
if(TARGET qcap::nvt_hdal)
  target_include_directories(qcap_demos_nvt_hdal INTERFACE
    "${NOVATEK_SDK}/include" "${NOVATEK_SDK}/vendor/media/include")
  target_link_directories(qcap_demos_nvt_hdal INTERFACE
    "${NOVATEK_SDK}/lib" "${NOVATEK_SDK}/vendor/media/lib")
endif()

# Library directories that are platform/sysroot-specific in the Make modules.
foreach(module IN ITEMS CUDA CUDA_DRIVER NVBUF SIPL)
  string(TOLOWER "${module}" module_lower)
  if(TARGET qcap::${module_lower})
    target_link_directories(qcap_demos_${module_lower} INTERFACE
      "${SDKTARGETSYSROOT}/usr/${LINUX_GNU_LIB}"
      "${SDKTARGETSYSROOT}/usr/${LINUX_GNU_LIB}/tegra"
      "${SDKTARGETSYSROOT}/usr/${LINUX_GNU_LIB}/nvidia")
  endif()
endforeach()
if(TARGET qcap::onetbb)
  target_include_directories(qcap_demos_onetbb INTERFACE /usr/local/qcap/usr/local/include)
  target_link_directories(qcap_demos_onetbb INTERFACE /usr/local/qcap/usr/local/lib)
endif()
if(TARGET qcap::ipp)
  if(BUILD_WITH_ONEAPI)
    target_include_directories(qcap_demos_ipp INTERFACE /opt/intel/oneapi/ipp/latest/include)
    target_link_directories(qcap_demos_ipp INTERFACE /opt/intel/oneapi/ipp/latest/lib/intel64)
  else()
    target_include_directories(qcap_demos_ipp INTERFACE /opt/intel/ipp/include)
    target_link_directories(qcap_demos_ipp INTERFACE /opt/intel/ipp/lib/intel64 /opt/intel/lib/intel64)
  endif()
endif()
