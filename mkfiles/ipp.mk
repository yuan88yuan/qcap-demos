ifeq (${BUILD_WITH_IPP},ON)

$(call decl_mod,IPP)

IPP_F+=\
-DBUILD_WITH_IPP=1

ifeq (${BUILD_WITH_ONEAPI},ON)

IPP_I+=\
-I/opt/intel/oneapi/ipp/latest/include

IPP_L+=$(call _ld_path,/opt/intel/oneapi/ipp/latest/lib/intel64/)

else # BUILD_WITH_ONEAPI

IPP_I+=\
-I/opt/intel/ipp/include

IPP_L+=$(call _ld_path,/opt/intel/ipp/lib/intel64/)
IPP_L+=$(call _ld_path,/opt/intel/lib/intel64/)

endif # BUILD_WITH_ONEAPI

IPP_L_A+=\
-lippcore \
-lippi \
-lippcc \
-lippvc \
-lipps \
-lippi \
-lirc \
-lsvml \
-lippcore

endif # BUILD_WITH_IPP