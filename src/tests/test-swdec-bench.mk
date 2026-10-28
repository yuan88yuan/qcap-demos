################## test-swdec-bench ##################
NVDEC_CLASS_DIR=/usr/src/jetson_multimedia_api/samples/common/classes

# The NVDEC helper classes are external sources supplied by the target platform
# (Jetson Multimedia API). Skip the whole module when the tree is unavailable so
# the module does not register an unbuildable target; mirrors the gate in
# cmake/QcapTargets.cmake.
ifneq ($(wildcard $(NVDEC_CLASS_DIR)/NvVideoDecoder.cpp),)

$(call decl_mod,TEST_SWDEC_BENCH)
$(call add_flags_mod,TEST_SWDEC_BENCH)
$(call add_mods,TEST_SWDEC_BENCH,ZZLAB QCAP)

TEST_SWDEC_BENCH_E=test-swdec-bench
TESTS+=$${TEST_SWDEC_BENCH_e}

TEST_SWDEC_BENCH_SRCS+=\
tests/test-swdec-bench.cpp

# NVDEC class objects (pre-compiled, linked directly)
NVDEC_CLASS_OBJS:=\
$(D)/nvdec_classes/NvVideoDecoder.o \
$(D)/nvdec_classes/NvV4l2Element.o \
$(D)/nvdec_classes/NvV4l2ElementPlane.o \
$(D)/nvdec_classes/NvElement.o \
$(D)/nvdec_classes/NvElementProfiler.o \
$(D)/nvdec_classes/NvBuffer.o \
$(D)/nvdec_classes/NvLogging.o \
$(D)/nvdec_classes/NvUtils.o \
$(D)/nvdec_classes/NvBufSurface.o

TEST_SWDEC_BENCH_I+=\
-I/usr/src/jetson_multimedia_api/include \
-I/usr/src/jetson_multimedia_api/include/libjpeg-8b \
-I$(NVDEC_CLASS_DIR)

TEST_SWDEC_BENCH_LDFLAGS+=\
-L/usr/lib/aarch64-linux-gnu/nvidia

# NVDEC class objects go before the static libs so they provide symbols
TEST_SWDEC_BENCH_LDFLAGS_A+=\
$(NVDEC_CLASS_OBJS) \
-Wl,--start-group \
-lavformat \
-lavcodec \
-lavutil \
-lswresample \
-lswscale \
-lpostproc \
-lfdk-aac \
-lssl \
-lcrypto \
-lz \
-Wl,--end-group

TEST_SWDEC_BENCH_LDFLAGS_S+=\
-lnvv4l2 \
-lnvbufsurface \
-lnvbufsurftransform

# Custom rules for NVDEC class compilation (absolute paths outside ${S})
NVDEC_CLASS_CXXFLAGS:=-std=c++17 -fPIC -fpermissive -Wno-write-strings \
	-I$(NVDEC_CLASS_DIR) \
	-I/usr/src/jetson_multimedia_api/include \
	-I/usr/src/jetson_multimedia_api/include/libjpeg-8b

$(D)/nvdec_classes/%.o: $(NVDEC_CLASS_DIR)/%.cpp
	$(info [TEST_SWDEC_BENCH] NVDEC CXX Compiling $@ ...)
	$(AT) mkdir -p $(dir $@)
	$(AT) $(CXX) $(NVDEC_CLASS_CXXFLAGS) -c $< -o $@

# Make the executable depend on NVDEC class objects
$(D)/bin/$(TEST_SWDEC_BENCH_E): $(NVDEC_CLASS_OBJS)

else # $(NVDEC_CLASS_DIR) unavailable

$(info [TEST_SWDEC_BENCH] Skipping target: $(NVDEC_CLASS_DIR) is unavailable on $(PLATFORM))

endif # Jetson Multimedia API availability
