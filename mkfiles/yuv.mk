ifeq (${BUILD_WITH_YUV},ON)

$(call decl_mod,YUV)

YUV_F+=\
-DBUILD_WITH_YUV=1

YUV_L_A+=\
-lyuv

endif