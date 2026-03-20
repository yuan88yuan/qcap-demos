
################## test-freetype ##################
$(call decl_mod,TEST_FREETYPE)
$(call add_flags_mod,TEST_FREETYPE)
$(call add_mods,TEST_FREETYPE,ZZLAB QCAP FONTCONFIG FREETYPE HARFBUZZ)

# $(info TEST_FREETYPE_FLAGS=${TEST_FREETYPE_FLAGS})

TEST_FREETYPE_E=test-freetype
TESTS+=$${TEST_FREETYPE_e}

TEST_FREETYPE_SRCS+=\
tests/test-freetype.cpp
