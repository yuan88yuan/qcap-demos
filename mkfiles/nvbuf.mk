ifeq (${BUILD_WITH_NVBUF},ON)

$(call decl_mod,NVBUF)

NVBUF_F+=\
-DBUILD_WITH_NVBUF=1

NVBUF_I+=\
-I${SDKTARGETSYSROOT}/usr/src/jetson_multimedia_api/include

NVBUF_L+=\
$(call _ld_path,${SDKTARGETSYSROOT}/usr/${LINUX_GNU_LIB}/tegra) \
$(call _ld_path,${SDKTARGETSYSROOT}/usr/${LINUX_GNU_LIB}/nvidia)

NVBUF_L_S+=\
-lnvbufsurface \
-lv4l2

endif