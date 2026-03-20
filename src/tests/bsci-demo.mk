
################## bsci-demo ##################
$(call decl_mod,BSCI_DEMO)
$(call add_flags_mod,BSCI_DEMO)
$(call add_mods,BSCI_DEMO,ZZLAB QCAP NVBUF CUDA NPP)

# $(info BSCI_DEMO_FLAGS=${BSCI_DEMO_FLAGS})

BSCI_DEMO_E=bsci-demo
TESTS+=$${BSCI_DEMO_e}

BSCI_DEMO_SRCS+=\
tests/bsci-demo.cpp
