
################## sc6f0-dante-tx ##################
$(call decl_mod,SC6F0_DANTE_TX)
$(call add_flags_mod,SC6F0_DANTE_TX)
$(call add_mods,SC6F0_DANTE_TX,ZZLAB QCAP ALLEGRO2 DAUSERVICE DRM ZSTREAMER)

# $(info SC6F0_DANTE_TX_FLAGS=${SC6F0_DANTE_TX_FLAGS})

SC6F0_DANTE_TX_E=sc6f0-dante-tx
TESTS+=$${SC6F0_DANTE_TX_e}

SC6F0_DANTE_TX_SRCS+=\
tests/sc6f0-dante-tx.cpp
