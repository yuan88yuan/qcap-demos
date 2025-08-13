ifeq (${BUILD_WITH_NVJPEG},ON)

$(call decl_mod,NVJPEG)

NVJPEG_F+=\
-DBUILD_WITH_NVJPEG=1 \
-DTEGRA_ACCELERATE

NVJPEG_L_S+=\
-lnvjpeg

endif