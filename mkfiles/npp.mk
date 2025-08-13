ifeq (${BUILD_WITH_NPP},ON)

$(call decl_mod,NPP)

NPP_F+=\
-DBUILD_WITH_NPP=1

NPP_L_S+=\
-lnppc \
-lnppial \
-lnppicc \
-lnppidei \
-lnppif \
-lnppig \
-lnppim \
-lnppist \
-lnppisu \
-lnppitc \
-lnpps

endif