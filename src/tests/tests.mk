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

################## test-store-pic ##################
$(call decl_mod,TEST_STORE_PIC)
$(call add_flags_mod,TEST_STORE_PIC)
$(call add_mods,TEST_STORE_PIC,ZZLAB QCAP QCAP2_LIC)

TEST_STORE_PIC_E=test-store-pic
TESTS+=$${TEST_STORE_PIC_e}

TEST_STORE_PIC_SRCS+=\
tests/test-store-pic.cpp
