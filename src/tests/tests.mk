################## test-avcap ##################
$(call decl_mod,TEST_AVCAP)
$(call add_flags_mod,TEST_AVCAP)
$(call add_mods,TEST_AVCAP,ZZLAB QCAP)

ifeq (${BUILD_SC6N0},ON)
$(call add_mods,TEST_AVCAP,NVBUF CUDA NPP)
endif # BUILD_SC6N0

# $(info TEST_AVCAP_FLAGS=${TEST_AVCAP_FLAGS})

TEST_AVCAP_E=test-avcap
TESTS+=$${TEST_AVCAP_e}

TEST_AVCAP_SRCS+=\
tests/test-avcap.cpp \
tests/test-avcap.cu

################## test-hsb-vsrc ##################
$(call decl_mod,TEST_HSB_VSRC)
$(call add_flags_mod,TEST_HSB_VSRC)
$(call add_mods,TEST_HSB_VSRC,ZZLAB QCAP CUDA FMT IBVERBS)

# $(info TEST_HSB_VSRC_FLAGS=${TEST_HSB_VSRC_FLAGS})

TEST_HSB_VSRC_E=test-hsb-vsrc
TESTS+=$${TEST_HSB_VSRC_e}

TEST_HSB_VSRC_SRCS+=\
tests/test-hsb-vsrc.cpp

################## test-deint ##################
$(call decl_mod,TEST_DEINT)
$(call add_flags_mod,TEST_DEINT)
$(call add_mods,TEST_DEINT,ZZLAB QCAP)

TEST_DEINT_E=test-deint
TESTS+=$${TEST_DEINT_e}

TEST_DEINT_SRCS+=\
tests/test-deint.cpp

################## test-lic ##################
$(call decl_mod,TEST_LIC)
$(call add_flags_mod,TEST_LIC)
$(call add_mods,TEST_LIC,ZZLAB QCAP QCAP2_LIC)

TEST_LIC_E=test-lic
TESTS+=$${TEST_LIC_e}

TEST_LIC_SRCS+=\
tests/test-lic.cpp

################## bsci-demo ##################
$(call decl_mod,BSCI_DEMO)
$(call add_flags_mod,BSCI_DEMO)
$(call add_mods,BSCI_DEMO,ZZLAB QCAP NVBUF CUDA NPP)

# $(info BSCI_DEMO_FLAGS=${BSCI_DEMO_FLAGS})

BSCI_DEMO_E=bsci-demo
TESTS+=$${BSCI_DEMO_e}

BSCI_DEMO_SRCS+=\
tests/bsci-demo.cpp

################## test-zznvenc2 ##################
$(call decl_mod,TEST_ZZNVENC2)
$(call add_flags_mod,TEST_ZZNVENC2)
$(call add_mods,TEST_ZZNVENC2,ZZLAB QCAP)

# $(info TEST_ZZNVENC2_FLAGS=${TEST_ZZNVENC2_FLAGS})

TEST_ZZNVENC2_E=test-zznvenc2
TESTS+=$${TEST_ZZNVENC2_e}

TEST_ZZNVENC2_SRCS+=\
tests/test-zznvenc2.cpp

################## test-ipx ##################
$(call decl_mod,TEST_IPX)
$(call add_flags_mod,TEST_IPX)
$(call add_mods,TEST_IPX,ZZLAB QCAP CUDA IPX)
$(call add_mods,TEST_IPX,NVBUF)

$(info TEST_IPX_FLAGS=${TEST_IPX_FLAGS})

TEST_IPX_E=test-ipx
TESTS+=$${TEST_IPX_e}

TEST_IPX_SRCS+=\
tests/test-ipx.cpp

################## kylin-demo ##################
$(call decl_mod,KYLIN_DEMO)
$(call add_flags_mod,KYLIN_DEMO)
$(call add_mods,KYLIN_DEMO,ZZLAB QCAP)

# $(info KYLIN_DEMO_FLAGS=${KYLIN_DEMO_FLAGS})

KYLIN_DEMO_E=kylin-demo
TESTS+=$${KYLIN_DEMO_e}

KYLIN_DEMO_SRCS+=\
tests/kylin-demo.cpp

################## sc6g0-aenc ##################
$(call decl_mod,SC6G0_AENC)
$(call add_flags_mod,SC6G0_AENC)
$(call add_mods,SC6G0_AENC,ZZLAB QCAP NVT_HDAL)

# $(info SC6G0_AENC_FLAGS=${SC6G0_AENC_FLAGS})

SC6G0_AENC_E=sc6g0-aenc
TESTS+=$${SC6G0_AENC_e}

SC6G0_AENC_SRCS+=\
tests/sc6g0-aenc.cpp

################## test-aenc ##################
$(call decl_mod,TEST_AENC)
$(call add_flags_mod,TEST_AENC)
$(call add_mods,TEST_AENC,ZZLAB QCAP NVT_HDAL)

# $(info TEST_AENC_FLAGS=${TEST_AENC_FLAGS})

TEST_AENC_E=test-aenc
TESTS+=$${TEST_AENC_e}

TEST_AENC_SRCS+=\
tests/test-aenc.cpp

################## test-freetype ##################
$(call decl_mod,TEST_FREETYPE)
$(call add_flags_mod,TEST_FREETYPE)
$(call add_mods,TEST_FREETYPE,ZZLAB QCAP FONTCONFIG FREETYPE HARFBUZZ)

# $(info TEST_FREETYPE_FLAGS=${TEST_FREETYPE_FLAGS})

TEST_FREETYPE_E=test-freetype
TESTS+=$${TEST_FREETYPE_e}

TEST_FREETYPE_SRCS+=\
tests/test-freetype.cpp

################## test-graphics ##################
$(call decl_mod,TEST_GRAPHICS)
$(call add_flags_mod,TEST_GRAPHICS)
$(call add_mods,TEST_GRAPHICS,ZZLAB QCAP)

ifeq (${BUILD_SC6G0},ON)
$(call add_mods,TEST_GRAPHICS,NVT_HDAL)
endif # BUILD_SC6G0

# $(info TEST_GRAPHICS_FLAGS=${TEST_GRAPHICS_FLAGS})

TEST_GRAPHICS_E=test-graphics
TESTS+=$${TEST_GRAPHICS_e}

TEST_GRAPHICS_SRCS+=\
tests/test-graphics.cpp

################## sc6f0-dante-demo ##################
$(call decl_mod,SC6F0_DANTE_DEMO)
$(call add_flags_mod,SC6F0_DANTE_DEMO)
$(call add_mods,SC6F0_DANTE_DEMO,ZZLAB QCAP ALLEGRO2 DAUSERVICE)

# $(info SC6F0_DANTE_DEMO_FLAGS=${SC6F0_DANTE_DEMO_FLAGS})

SC6F0_DANTE_DEMO_E=sc6f0-dante-demo
TESTS+=$${SC6F0_DANTE_DEMO_e}

SC6F0_DANTE_DEMO_SRCS+=\
tests/sc6f0-dante-demo.cpp

################## coe-demo ##################
$(call decl_mod,COE_DEMO)
$(call add_flags_mod,COE_DEMO)
$(call add_mods,COE_DEMO,ZZLAB QCAP CUDA CUDA_DRIVER SIPL)

# $(info COE_DEMO_FLAGS=${COE_DEMO_FLAGS})

COE_DEMO_E=coe-demo
TESTS+=$${COE_DEMO_e}

COE_DEMO_SRCS+=\
tests/coe-demo.cpp
