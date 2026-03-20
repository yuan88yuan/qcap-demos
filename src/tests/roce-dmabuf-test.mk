
################## roce-dmabuf-test ##################
$(call decl_mod,ROCE_DMABUF_TEST)
$(call add_flags_mod,ROCE_DMABUF_TEST)
$(call add_mods,ROCE_DMABUF_TEST,ZZLAB QCAP CUDA CUDA_DRIVER)

# $(info ROCE_DMABUF_TEST_FLAGS=${ROCE_DMABUF_TEST_FLAGS})

ROCE_DMABUF_TEST_E=roce-dmabuf-test
TESTS+=$${ROCE_DMABUF_TEST_e}

ROCE_DMABUF_TEST_SRCS+=\
tests/roce-dmabuf-test.cpp
