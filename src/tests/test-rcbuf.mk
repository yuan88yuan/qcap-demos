
################## test-rcbuf ##################
$(call decl_mod,TEST_RCBUF)
$(call add_flags_mod,TEST_RCBUF)
$(call add_mods,TEST_RCBUF,ZZLAB QCAP)

# $(info TEST_RCBUF_FLAGS=${TEST_RCBUF_FLAGS})

TEST_RCBUF_E=test-rcbuf
TESTS+=$${TEST_RCBUF_e}

TEST_RCBUF_SRCS+=\
tests/test-rcbuf.cpp
