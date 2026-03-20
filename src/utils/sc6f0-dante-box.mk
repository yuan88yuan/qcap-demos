################## sc6f0-dante-box ##################
$(call decl_mod,SC6F0_DANTE_BOX)
$(call add_flags_mod,SC6F0_DANTE_BOX)
$(call add_mods,SC6F0_DANTE_BOX,QCAP ZZLAB)

# $(info SC6F0_DANTE_BOX_FLAGS=${SC6F0_DANTE_BOX_FLAGS})

SC6F0_DANTE_BOX_E=sc6f0-dante-box
UTILS+=$${SC6F0_DANTE_BOX_e}

SC6F0_DANTE_BOX_SRCS+=\
utils/sc6f0-dante-box.cpp
