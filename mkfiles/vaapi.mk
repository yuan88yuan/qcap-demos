ifeq (${BUILD_WITH_VAAPI},ON)

$(call decl_mod,VAAPI)

VAAPI_F+=\
-DBUILD_WITH_VAAPI=1

VAAPI_L_S+=\
-lva \
-lva-drm \
-lva-x11

endif