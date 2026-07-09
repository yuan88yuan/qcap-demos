ifeq (${BUILD_WITH_FREETYPE},ON)

$(call decl_mod,FREETYPE)

FREETYPE_F+=\
-DBUILD_WITH_FREETYPE=1

FREETYPE_I+=\
-I/opt/qcap/qcap-3rdparty/${PLATFORM}/include/freetype2

FREETYPE_L_A+=\
-lfreetype

endif