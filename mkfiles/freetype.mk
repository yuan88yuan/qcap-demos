ifeq (${BUILD_WITH_FREETYPE},ON)

$(call decl_mod,FREETYPE)

FREETYPE_F+=\
-DBUILD_WITH_FREETYPE=1

FREETYPE_I+=\
-I${DOCKER_HOME}/qcap-3rdparty/${PLATFORM}/include/freetype2

FREETYPE_WITH_PNG=\
l4t-r35-1

ifneq (,$(filter $(FREETYPE_WITH_PNG),$(PLATFORM)))
$(info !!! freetype built with png !!!)

FREETYPE_L_A+=\
-lpng \
-lz
endif

FREETYPE_L_A+=\
-lfreetype

endif