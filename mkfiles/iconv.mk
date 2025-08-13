BUILD_WITH_ICONV=1

$(call decl_mod,ICONV)

ICONV_F+=\
-DBUILD_WITH_ICONV=1

ICONV_L_A+=\
-liconv
