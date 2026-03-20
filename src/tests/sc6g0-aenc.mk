
################## sc6g0-aenc ##################
$(call decl_mod,SC6G0_AENC)
$(call add_flags_mod,SC6G0_AENC)
$(call add_mods,SC6G0_AENC,ZZLAB QCAP NVT_HDAL)

# $(info SC6G0_AENC_FLAGS=${SC6G0_AENC_FLAGS})

SC6G0_AENC_E=sc6g0-aenc
TESTS+=$${SC6G0_AENC_e}

SC6G0_AENC_SRCS+=\
tests/sc6g0-aenc.cpp
