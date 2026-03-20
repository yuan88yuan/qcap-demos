
################## test-aenc ##################
$(call decl_mod,TEST_AENC)
$(call add_flags_mod,TEST_AENC)
$(call add_mods,TEST_AENC,ZZLAB QCAP NVT_HDAL)

# $(info TEST_AENC_FLAGS=${TEST_AENC_FLAGS})

TEST_AENC_E=test-aenc
TESTS+=$${TEST_AENC_e}

TEST_AENC_SRCS+=\
tests/test-aenc.cpp
