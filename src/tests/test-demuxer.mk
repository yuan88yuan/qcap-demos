
################## test-demuxer ##################
$(call decl_mod,TEST_DEMUXER)
$(call add_flags_mod,TEST_DEMUXER)
$(call add_mods,TEST_DEMUXER,ZZLAB QCAP ALLEGRO2)

# $(info TEST_DEMUXER_FLAGS=${TEST_DEMUXER_FLAGS})

TEST_DEMUXER_E=test-demuxer
TESTS+=$${TEST_DEMUXER_e}

TEST_DEMUXER_SRCS+=\
tests/test-demuxer.cpp
