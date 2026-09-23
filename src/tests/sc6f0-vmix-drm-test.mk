
################## sc6f0-vmix-drm-test ##################
$(call decl_mod,SC6F0_VMIX_DRM_TEST)
$(call add_flags_mod,SC6F0_VMIX_DRM_TEST)
$(call add_mods,SC6F0_VMIX_DRM_TEST,ZZLAB QCAP DRM)

SC6F0_VMIX_DRM_TEST_E=sc6f0-vmix-drm-test
TESTS+=$${SC6F0_VMIX_DRM_TEST_e}

SC6F0_VMIX_DRM_TEST_SRCS+=\
tests/sc6f0-vmix-drm-test.cpp
