ifeq (${BUILD_WITH_GSTREAMER},ON)

$(call decl_mod,GSTREAMER)

GSTREAMER_F+=\
-DBUILD_WITH_GSTREAMER=1

GSTREAMER_I+=\
-I${SDKTARGETSYSROOT}/usr/include/ \
-I${SDKTARGETSYSROOT}/usr/include/glib-2.0 \
-I${SDKTARGETSYSROOT}/usr/include/gstreamer-1.0/ \
-I${SDKTARGETSYSROOT}/usr/${LINUX_GNU_LIB}/glib-2.0/include \
-I${SDKTARGETSYSROOT}/usr/${LINUX_GNU_LIB}/gstreamer-1.0/include

GSTREAMER_L_S+=\
-lgstreamer-1.0 \
-lgobject-2.0 \
-lglib-2.0 \
-lgstallocators-1.0 \
-lgstvideo-1.0 \
-lgstapp-1.0

endif