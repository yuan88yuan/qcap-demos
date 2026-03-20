
################## test-deint ##################
$(call decl_mod,TEST_DEINT)
$(call add_flags_mod,TEST_DEINT)
$(call add_mods,TEST_DEINT,ZZLAB QCAP)

TEST_DEINT_E=test-deint
TESTS+=$${TEST_DEINT_e}

TEST_DEINT_SRCS+=\
tests/test-deint.cpp
