ifeq (${BUILD_WITH_IBVERBS},ON)

$(call decl_mod,IBVERBS)

IBVERBS_F+=\
-DBUILD_WITH_IBVERBS=1

CUDA_L_S+=\
-libverbs

endif
