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

endif