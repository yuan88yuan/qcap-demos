ifeq (${BUILD_WITH_SPEEXDSP},ON)

$(call decl_mod,SPEEXDSP)

SPEEXDSP_F+=\
-DBUILD_WITH_SPEEXDSP=1

SPEEXDSP_L_S+=\
-lspeexdsp

endif