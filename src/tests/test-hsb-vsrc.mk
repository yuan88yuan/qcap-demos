
################## test-hsb-vsrc ##################
$(call decl_mod,TEST_HSB_VSRC)
$(call add_flags_mod,TEST_HSB_VSRC)
$(call add_mods,TEST_HSB_VSRC,ZZLAB QCAP CUDA FMT IBVERBS)

# $(info TEST_HSB_VSRC_FLAGS=${TEST_HSB_VSRC_FLAGS})

TEST_HSB_VSRC_E=test-hsb-vsrc
TESTS+=$${TEST_HSB_VSRC_e}

TEST_HSB_VSRC_SRCS+=\
tests/test-hsb-vsrc.cpp
