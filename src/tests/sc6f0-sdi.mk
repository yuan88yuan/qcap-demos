
################## sc6f0-sdi ##################
$(call decl_mod,SC6F0_SDI)
$(call add_flags_mod,SC6F0_SDI)
$(call add_mods,SC6F0_SDI,ZZLAB QCAP)

# $(info SC6F0_SDI_FLAGS=${SC6F0_SDI_FLAGS})

SC6F0_SDI_E=sc6f0-sdi
TESTS+=$${SC6F0_SDI_e}

SC6F0_SDI_SRCS+=\
tests/sc6f0-sdi.cpp
