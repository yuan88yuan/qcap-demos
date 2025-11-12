ifeq (${BUILD_WITH_IPX},ON)

$(call decl_mod,IPX)

IPX_F+=\
-DBUILD_WITH_IPX=1

IPX_L_S+=\
-lIpxGpuCodec

endif
