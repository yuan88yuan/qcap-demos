
################## demo-template ##################
$(call decl_mod,DEMO_TEMPLATE)
$(call add_flags_mod,DEMO_TEMPLATE)
$(call add_mods,DEMO_TEMPLATE,ZZLAB QCAP)

# $(info DEMO_TEMPLATE_FLAGS=${DEMO_TEMPLATE_FLAGS})

DEMO_TEMPLATE_E=demo-template
TESTS+=$${DEMO_TEMPLATE_e}

DEMO_TEMPLATE_SRCS+=\
tests/demo-template.cpp
