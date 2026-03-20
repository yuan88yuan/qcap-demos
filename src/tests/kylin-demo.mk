
################## kylin-demo ##################
$(call decl_mod,KYLIN_DEMO)
$(call add_flags_mod,KYLIN_DEMO)
$(call add_mods,KYLIN_DEMO,ZZLAB QCAP)

# $(info KYLIN_DEMO_FLAGS=${KYLIN_DEMO_FLAGS})

KYLIN_DEMO_E=kylin-demo
TESTS+=$${KYLIN_DEMO_e}

KYLIN_DEMO_SRCS+=\
tests/kylin-demo.cpp
