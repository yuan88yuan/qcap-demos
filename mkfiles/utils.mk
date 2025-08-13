################## qdemo ##################
$(call decl_mod,QDEMO)
$(call add_flags_mod,QDEMO)
$(call add_mods,QDEMO,BASE BOOST QCAP)

QDEMO_E=qdemo
UTILS+=$${QDEMO_e}

QDEMO_SRCS+=\
qdemo/main.cpp
