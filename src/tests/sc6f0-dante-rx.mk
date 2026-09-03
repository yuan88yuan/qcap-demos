
################## sc6f0-dante-rx ##################
$(call decl_mod,SC6F0_DANTE_RX)
$(call add_flags_mod,SC6F0_DANTE_RX)
$(call add_mods,SC6F0_DANTE_RX,ZZLAB QCAP ALSA ALLEGRO2 DAUSERVICE DRM ZSTREAMER)

# $(info SC6F0_DANTE_RX_FLAGS=${SC6F0_DANTE_RX_FLAGS})

SC6F0_DANTE_RX_E=sc6f0-dante-rx
TESTS+=$${SC6F0_DANTE_RX_e}

SC6F0_DANTE_RX_SRCS+=\
tests/sc6f0-dante-rx.cpp
