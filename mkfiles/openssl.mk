ifeq (${BUILD_WITH_OPENSSL},ON)

$(call decl_mod,OPENSSL)

OPENSSL_F+=\
-DBUILD_WITH_OPENSSL=1

OPENSSL_L_A+=\
-lssl \
-lcrypto

endif