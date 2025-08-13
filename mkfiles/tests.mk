################## test-avcap ##################
$(call decl_mod,TEST_AVCAP)
$(call add_flags_mod,TEST_AVCAP)
$(call add_mods,TEST_AVCAP,ZZLAB QCAP NVBUF CUDA NPP)

$(info TEST_AVCAP_FLAGS=${TEST_AVCAP_FLAGS})

TEST_AVCAP_E=test-avcap
TESTS+=$${TEST_AVCAP_e}

TEST_AVCAP_SRCS+=\
tests/test-avcap.cpp \
tests/test-avcap.cu
