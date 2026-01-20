ifeq (${BUILD_WITH_CUDA_DRIVER},ON)

$(call decl_mod,CUDA_DRIVER)

CUDA_DRIVER_F+=\
-DBUILD_WITH_CUDA_DRIVER=1

CUDA_DRIVER_I+=\
-I$(SDKTARGETSYSROOT)/usr/local/cuda/include

CUDA_DRIVER_L+=\
$(call _ld_path,${SDKTARGETSYSROOT}/usr/${LINUX_GNU_LIB})

CUDA_DRIVER_L_S+=\
-lcuda

endif