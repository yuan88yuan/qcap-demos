################## test-vdec-bench ##################
$(call decl_mod,TEST_VDEC_BENCH)
$(call add_flags_mod,TEST_VDEC_BENCH)
$(call add_mods,TEST_VDEC_BENCH,ZZLAB QCAP)

TEST_VDEC_BENCH_E=test-vdec-bench
TESTS+=$${TEST_VDEC_BENCH_e}

TEST_VDEC_BENCH_SRCS+=\
tests/test-vdec-bench.cpp
