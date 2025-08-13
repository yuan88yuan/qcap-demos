ifeq (${BUILD_WITH_FMT},ON)

$(call decl_mod,FMT)

FMT_F+=\
-DBUILD_WITH_FMT=1

FMT_L_S+=\
-lfmt

endif
