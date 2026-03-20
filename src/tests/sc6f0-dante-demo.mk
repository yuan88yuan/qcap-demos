
################## sc6f0-dante-demo ##################
$(call decl_mod,SC6F0_DANTE_DEMO)
$(call add_flags_mod,SC6F0_DANTE_DEMO)
$(call add_mods,SC6F0_DANTE_DEMO,ZZLAB QCAP ALLEGRO2 DAUSERVICE DRM)

# $(info SC6F0_DANTE_DEMO_FLAGS=${SC6F0_DANTE_DEMO_FLAGS})

SC6F0_DANTE_DEMO_E=sc6f0-dante-demo
TESTS+=$${SC6F0_DANTE_DEMO_e}

SC6F0_DANTE_DEMO_SRCS+=\
tests/sc6f0-dante-demo.cpp
