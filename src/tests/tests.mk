ifeq (${BUILD_WITH_CUDA},ON)

################## test-avcap ##################
$(call decl_mod,TEST_AVCAP)
$(call add_flags_mod,TEST_AVCAP)
$(call add_mods,TEST_AVCAP,ZZLAB QCAP NVBUF CUDA NPP)

# $(info TEST_AVCAP_FLAGS=${TEST_AVCAP_FLAGS})

TEST_AVCAP_E=test-avcap
TESTS+=$${TEST_AVCAP_e}

TEST_AVCAP_SRCS+=\
tests/test-avcap.cpp \
tests/test-avcap.cu

endif # BUILD_WITH_CUDA

ifeq (${BUILD_WITH_IBVERBS},ON)

################## test-hsb-vsrc ##################
$(call decl_mod,TEST_HSB_VSRC)
$(call add_flags_mod,TEST_HSB_VSRC)
$(call add_mods,TEST_HSB_VSRC,ZZLAB QCAP CUDA FMT IBVERBS)

$(info TEST_HSB_VSRC_FLAGS=${TEST_HSB_VSRC_FLAGS})

TEST_HSB_VSRC_E=test-hsb-vsrc
TESTS+=$${TEST_HSB_VSRC_e}

TEST_HSB_VSRC_SRCS+=\
tests/test-hsb-vsrc.cpp

endif # BUILD_WITH_IBVERBS

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

ifeq (${BUILD_WITH_IPX},ON)

################## test-ipx ##################
$(call decl_mod,TEST_IPX)
$(call add_flags_mod,TEST_IPX)
$(call add_mods,TEST_IPX,ZZLAB QCAP NVBUF CUDA IPX)

$(info TEST_IPX_FLAGS=${TEST_IPX_FLAGS})

TEST_IPX_E=test-ipx
TESTS+=$${TEST_IPX_e}

TEST_IPX_SRCS+=\
tests/test-ipx.cpp

endif # BUILD_WITH_IPX

################## kylin-demo ##################
$(call decl_mod,KYLIN_DEMO)
$(call add_flags_mod,KYLIN_DEMO)
$(call add_mods,KYLIN_DEMO,ZZLAB QCAP)

# $(info KYLIN_DEMO_FLAGS=${KYLIN_DEMO_FLAGS})

KYLIN_DEMO_E=kylin-demo
TESTS+=$${KYLIN_DEMO_e}

KYLIN_DEMO_SRCS+=\
tests/kylin-demo.cpp
