
################## test-alsa-400hz ##################
$(call decl_mod,TEST_ALSA_400HZ)
$(call add_flags_mod,TEST_ALSA_400HZ)
$(call add_mods,TEST_ALSA_400HZ,ZZLAB QCAP ALSA)

# $(info TEST_ALSA_400HZ_FLAGS=${TEST_ALSA_400HZ_FLAGS})

TEST_ALSA_400HZ_E=test-alsa-400hz
TESTS+=$${TEST_ALSA_400HZ_e}

TEST_ALSA_400HZ_SRCS+=\
tests/test-alsa-400hz.cpp

TEST_ALSA_400HZ_LDFLAGS_S+=\
-lm
