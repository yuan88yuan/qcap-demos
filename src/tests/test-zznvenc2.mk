
################## test-zznvenc2 ##################
$(call decl_mod,TEST_ZZNVENC2)
$(call add_flags_mod,TEST_ZZNVENC2)
$(call add_mods,TEST_ZZNVENC2,ZZLAB QCAP)

# $(info TEST_ZZNVENC2_FLAGS=${TEST_ZZNVENC2_FLAGS})

TEST_ZZNVENC2_E=test-zznvenc2
TESTS+=$${TEST_ZZNVENC2_e}

TEST_ZZNVENC2_SRCS+=\
tests/test-zznvenc2.cpp
