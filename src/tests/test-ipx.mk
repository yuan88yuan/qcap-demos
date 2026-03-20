
################## test-ipx ##################
$(call decl_mod,TEST_IPX)
$(call add_flags_mod,TEST_IPX)
$(call add_mods,TEST_IPX,ZZLAB QCAP CUDA IPX)
$(call add_mods,TEST_IPX,NVBUF)

# $(info TEST_IPX_FLAGS=${TEST_IPX_FLAGS})

TEST_IPX_E=test-ipx
TESTS+=$${TEST_IPX_e}

TEST_IPX_SRCS+=\
tests/test-ipx.cpp
