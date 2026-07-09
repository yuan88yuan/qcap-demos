ifeq (${BUILD_WITH_HARFBUZZ},ON)

$(call decl_mod,HARFBUZZ)

HARFBUZZ_F+=\
-DBUILD_WITH_HARFBUZZ=1

HARFBUZZ_I+=\
-I/opt/qcap/qcap-3rdparty/${PLATFORM}/include/harfbuzz

HARFBUZZ_L_A+=\
-lharfbuzz

# fix symbolic errors for some compilers
HARFBUZZ_L_A+=\
${FREETYPE_L_A}

endif