ifeq (${BUILD_WITH_QCAP},ON)

$(call decl_mod,QCAP)

QCAP_F+=\
-DBUILD_WITH_QCAP=1

QCAP_I+=\
-I${QCAP_HOME}/include

QCAP_L+=$(call _ld_path,${QCAP_HOME}/lib)

QCAP_L_S+=\
-lqcap

endif