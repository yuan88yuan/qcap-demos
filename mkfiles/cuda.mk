ifeq (${BUILD_WITH_CUDA},ON)

$(call decl_mod,CUDA)

CUDA_F+=\
-DBUILD_WITH_CUDA=1

CUDA_I+=\
-I$(SDKTARGETSYSROOT)/usr/local/cuda/include

CUDA_L+=\
$(call _ld_path,$(SDKTARGETSYSROOT)/usr/local/cuda/lib64)

CUDA_L_S+=\
-lcudart

WITH_CUDA_DRIVER_API=\
igx-r36-3

ifneq (,$(filter $(WITH_CUDA_DRIVER_API),$(PLATFORM)))
$(info !!! cuda driver-api !!!)

CUDA_L+=\
$(call _ld_path,${SDKTARGETSYSROOT}/usr/${LINUX_GNU_LIB})

CUDA_L_S+=\
-lcuda

endif

endif