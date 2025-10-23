ifeq (${BUILD_WITH_QCAP2_LIC},ON)

$(call decl_mod,QCAP2_LIC)

QCAP2_LIC_F+=\
-DBUILD_QCAP2=1 \
-DBUILD_WITH_QCAP2_LIC=1

QCAP2_LIC_I+=\
-I${QCAP_HOME}/include

QCAP2_LIC_L+=$(call _ld_path,${QCAP_HOME}/lib)

QCAP2_LIC_L_S+=\
-lqcap2_lic

endif