################## test-rtsp ##################
$(call decl_mod,TEST_RTSP)
$(call add_flags_mod,TEST_RTSP)
$(call add_mods,TEST_RTSP,ZZLAB QCAP)

TEST_RTSP_E=test-rtsp
TESTS+=$${TEST_RTSP_e}

TEST_RTSP_SRCS+=\
tests/test-rtsp.cpp
