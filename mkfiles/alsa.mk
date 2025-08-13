ifeq (${BUILD_WITH_ALSA},ON)

$(call decl_mod,ALSA)

ALSA_F+=\
-DBUILD_WITH_ALSA=1

ALSA_SUPPORT=\
novatek_arm64

# alsa support
ifneq (,$(filter $(ALSA_SUPPORT),$(PLATFORM)))
$(info !!! alsa support !!!)

ALSA_L_A+=\
-lasound
else

ALSA_L_S+=\
-lasound
endif

endif