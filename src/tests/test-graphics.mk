
################## test-graphics ##################
$(call decl_mod,TEST_GRAPHICS)
$(call add_flags_mod,TEST_GRAPHICS)
$(call add_mods,TEST_GRAPHICS,ZZLAB QCAP)

ifeq (${BUILD_SC6G0},ON)
$(call add_mods,TEST_GRAPHICS,NVT_HDAL)
endif # BUILD_SC6G0

# $(info TEST_GRAPHICS_FLAGS=${TEST_GRAPHICS_FLAGS})

TEST_GRAPHICS_E=test-graphics
TESTS+=$${TEST_GRAPHICS_e}

TEST_GRAPHICS_SRCS+=\
tests/test-graphics.cpp
