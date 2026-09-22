
################## test-qcap-400hz ##################
$(call decl_mod,TEST_QCAP_400HZ)
$(call add_flags_mod,TEST_QCAP_400HZ)
$(call add_mods,TEST_QCAP_400HZ,ZZLAB QCAP)

# $(info TEST_QCAP_400HZ_FLAGS=${TEST_QCAP_400HZ_FLAGS})

TEST_QCAP_400HZ_E=test-qcap-400hz
TESTS+=$${TEST_QCAP_400HZ_e}

TEST_QCAP_400HZ_SRCS+=\
tests/test-qcap-400hz.cpp

TEST_QCAP_400HZ_LDFLAGS_S+=\
-lm
