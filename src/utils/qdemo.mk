################## qdemo ##################
$(call decl_mod,QDEMO)
$(call add_flags_mod,QDEMO)
$(call add_mods,QDEMO,QCAP ZZLAB)

# $(info QDEMO_FLAGS=${QDEMO_FLAGS})

QDEMO_E=qdemo
UTILS+=$${QDEMO_e}

QDEMO_SRCS+=\
utils/qdemo.cpp
