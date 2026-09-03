ifeq (${BUILD_WITH_ZSTREAMER},ON)

$(call decl_mod,ZSTREAMER)

ZSTREAMER_ROOT?=${S}/../../zstreamer

ZSTREAMER_F+=\
-DBUILD_WITH_ZSTREAMER=1

ZSTREAMER_I+=\
-I${ZSTREAMER_ROOT}/include \
-I${ZSTREAMER_ROOT}/include/zstreamer/elements

ZSTREAMER_L+=\
$(call _ld_path,${ZSTREAMER_ROOT}/build-arm64/src)

ZSTREAMER_L_A+=\
-lzstreamer-elements \
-lzstreamer \
/opt/qcap/qcap-3rdparty/${PLATFORM}/lib/libswresample.a \
/opt/qcap/qcap-3rdparty/${PLATFORM}/lib/libavutil.a

ZSTREAMER_L_S+=\
-ljson-c -lpthread -lm -lrt -ldl

endif
