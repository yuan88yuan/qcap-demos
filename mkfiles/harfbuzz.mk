ifeq (${BUILD_WITH_HARFBUZZ},ON)

$(call decl_mod,HARFBUZZ)

HARFBUZZ_F+=\
-DBUILD_WITH_HARFBUZZ=1

HARFBUZZ_I+=\
-I${QCAP_3RDPARTY}/include/harfbuzz

HARFBUZZ_L_A+=\
-lharfbuzz

HARFBUZZ_SUBSET_SUPPORT=\
hi3531a \
hi3531a510 \
hi3519a \
igx-r36-0-7-0 \
igx-r36-3 \
l4t-r35-1 \
l4t-r36-2 \
kylin_arm64 \
novatek_arm64

# harfbuzz-subset support
ifneq (,$(filter $(HARFBUZZ_SUBSET_SUPPORT),$(PLATFORM)))
$(info !!! harfbuzz-subset !!!)

HARFBUZZ_L_A+=\
-lharfbuzz-subset

endif

# fix symbolic errors for some compilers
HARFBUZZ_L_A+=\
${FREETYPE_L_A}

endif