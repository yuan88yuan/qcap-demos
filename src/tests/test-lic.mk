
################## test-lic ##################
$(call decl_mod,TEST_LIC)
$(call add_flags_mod,TEST_LIC)
$(call add_mods,TEST_LIC,ZZLAB QCAP QCAP2_LIC)

TEST_LIC_E=test-lic
TESTS+=$${TEST_LIC_e}

TEST_LIC_SRCS+=\
tests/test-lic.cpp
