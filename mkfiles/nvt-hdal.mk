ifeq (${BUILD_WITH_NVT_HDAL},ON)

$(call decl_mod,NVT_HDAL)

NVT_HDAL_F+=\
-DBUILD_WITH_NVT_HDAL=1

NVT_HDAL_I+=\
-I${NOVATEK_SDK}/include \
-I${NOVATEK_SDK}/vendor/media/include

NVT_HDAL_L+=\
$(call _ld_path,${NOVATEK_SDK}/lib) \
$(call _ld_path,${NOVATEK_SDK}/vendor/media/lib)

NVT_HDAL_L_S+=\
-lhdal \
-lvendor_media \
-lbrnvt

endif # BUILD_WITH_NVT_HDAL
