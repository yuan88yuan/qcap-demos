ifeq (${BUILD_WITH_DRM},ON)

$(call decl_mod,DRM)

DRM_F+=\
-DBUILD_WITH_DRM=1

DRM_I+=\
-I${SDKTARGETSYSROOT}/usr/include/drm

DRM_L_S+=\
-ldrm

endif