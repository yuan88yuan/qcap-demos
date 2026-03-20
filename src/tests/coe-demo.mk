
################## coe-demo ##################
$(call decl_mod,COE_DEMO)
$(call add_flags_mod,COE_DEMO)
$(call add_mods,COE_DEMO,ZZLAB QCAP CUDA CUDA_DRIVER SIPL)

# $(info COE_DEMO_FLAGS=${COE_DEMO_FLAGS})

COE_DEMO_E=coe-demo
TESTS+=$${COE_DEMO_e}

COE_DEMO_SRCS+=\
tests/coe-demo.cpp
