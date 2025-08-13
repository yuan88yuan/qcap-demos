ifeq (${BUILD_WITH_QCAP},ON)

$(call decl_mod,QCAP)

QCAP_F+=\
-DBUILD_WITH_QCAP=1

QCAP_L_A+=\
-lqcap

endif