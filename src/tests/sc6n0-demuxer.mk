################## sc6n0-demuxer ##################
$(call decl_mod,SC6N0_DEMUXER)
$(call add_flags_mod,SC6N0_DEMUXER)
$(call add_mods,SC6N0_DEMUXER,ZZLAB QCAP)

SC6N0_DEMUXER_E=sc6n0-demuxer
TESTS+=$${SC6N0_DEMUXER_e}

SC6N0_DEMUXER_SRCS+=\
tests/sc6n0-demuxer.cpp
