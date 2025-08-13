ifeq (${BUILD_WITH_ONETBB},ON)

$(call decl_mod,ONETBB)

ONETBB_F+=\
-DBUILD_WITH_ONETBB=1

ONETBB_I+=\
-L/usr/local/qcap/usr/local/include

ONETBB_L+=\
$(call _ld_path,/usr/local/qcap/usr/local/lib)

ONETBB_L_S+=\
-ltbb

define ONETBB_INSTALL
$(info Installing onetbb shared objects ...)
${AT} mkdir ${D}/lib -p
${AT} cp /usr/local/qcap/lib/libtbb*.so* ${D}/lib/ -af

endef

endif