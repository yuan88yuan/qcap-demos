ifeq (${BUILD_WITH_SIPL},ON)

$(call decl_mod,SIPL)

SIPL_F+=\
-DBUILD_WITH_SIPL=1

SIPL_I+=\
-I${SDKTARGETSYSROOT}/usr/src/jetson_sipl_api/sipl/include/nvsci \
-I${SDKTARGETSYSROOT}/usr/src/jetson_sipl_api/sipl/include/nvmedia \
-I${SDKTARGETSYSROOT}/usr/src/jetson_sipl_api/sipl/include \
-I${SDKTARGETSYSROOT}/usr/src/jetson_sipl_api/sipl/include/query/include

SIPL_L+=\
$(call _ld_path,${SDKTARGETSYSROOT}/usr/${LINUX_GNU_LIB}/nvidia)

SIPL_L_S+=\
-lnvscibuf \
-lnvsipl \
-lnvsipl_query \

endif