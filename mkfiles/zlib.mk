BUILD_WITH_ZLIB=ON

$(call decl_mod,ZLIB)

ZLIB_F+=\
-DBUILD_WITH_ZLIB=1

ZLIB_L_A+=\
-lz
