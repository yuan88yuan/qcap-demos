/*
 * test-swdec-bench.cpp - Video decode benchmark (software + hardware)
 *
 * Test Case 1: qcap2 software decoder (QCAP_ENCODER_TYPE_SOFTWARE)
 * Test Case 2: Jetson Multimedia API hardware decoder (NvVideoDecoder / NVDEC)
 *
 * Uses FFmpeg libavformat for stream probing.
 * Supports multiple concurrent decoder instances in separate threads.
 *
 * Usage: ./test-swdec-bench <input.h264> [iterations] [instances]
 *   Select test case via TEST_CASE env var or interactive menu.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <fstream>
#include <vector>
#include <string>
#include <iostream>
#include <sstream>
#include <thread>
#include <atomic>
#include <algorithm>
#include <functional>
#include <memory>
#include <cstring>
#include <cerrno>
#include <semaphore.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
}

#include "qcap2.h"
#include "qcap2.user.h"

/* Jetson Multimedia API */
#include "NvVideoDecoder.h"
#include "NvUtils.h"
#include "NvBufSurface.h"
#include "nvbufsurface.h"
#include "nvbufsurftransform.h"
#include <linux/videodev2.h>

#include "ZzLog.h"
#include "ZzModules.h"
#include "ZzClock.h"
#include "ZzStats.h"
#include "ZzUtils.h"
#include "testkit.h"

ZZ_INIT_LOG("test-swdec-bench");

int g_argc = 0;
char** g_argv = NULL;

ZZ_MODULE_DECL(__zz_log__);

using namespace __zz_clock__;
using __testkit__::wait_for_test_finish;
using __testkit__::TestCase;
using __testkit__::free_stack_t;
using __testkit__::tick_ctrl_t;
using __testkit__::NewEvent;
using __testkit__::AddEventHandler;

namespace __test_swdec_bench__ {
	ZZ_MODULES_INIT();
	struct modules_init_t;

	struct modules_init_t {
		modules_init_t() {
			ZZ_MODULE_INIT(__zz_log__);
		}

		~modules_init_t() {
			ZZ_MODULES_UNINIT();
		}
	};
}

using namespace __test_swdec_bench__;

/* ====================================================================== */
/* Shared: Pre-loaded bitstream and FFmpeg probe                          */
/* ====================================================================== */

static std::vector<uint8_t> g_bitstream_data;
static int g_stream_frames = 0;
static int g_stream_width = 0;
static int g_stream_height = 0;

/* NAL unit with start code */
struct nal_unit_t {
	std::vector<uint8_t> data;
};

/* Per-instance result */
typedef struct {
	int    num_frames;
	double decode_ms;
	bool   got_error;
} inst_result_t;

/*
 * Probe the stream using FFmpeg to get resolution and frame count.
 */
static int
probe_stream()
{
	AVFormatContext *fmt_ctx = avformat_alloc_context();
	if (!fmt_ctx) return -1;

	fmt_ctx->pb = avio_alloc_context(
		(unsigned char *)g_bitstream_data.data(),
		g_bitstream_data.size(),
		0, nullptr, nullptr, nullptr, nullptr);
	if (!fmt_ctx->pb) { avformat_free_context(fmt_ctx); return -1; }

	const AVInputFormat *fmt = av_find_input_format("h264");
	if (avformat_open_input(&fmt_ctx, "", fmt, nullptr) < 0) {
		avformat_free_context(fmt_ctx);
		return -1;
	}
	avformat_find_stream_info(fmt_ctx, nullptr);

	for (unsigned i = 0; i < fmt_ctx->nb_streams; i++) {
		if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
			g_stream_width = fmt_ctx->streams[i]->codecpar->width;
			g_stream_height = fmt_ctx->streams[i]->codecpar->height;
			g_stream_frames = fmt_ctx->streams[i]->nb_frames;
			break;
		}
	}

	/* If nb_frames not known, decode to count */
	if (g_stream_frames <= 0) {
		const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_H264);
		AVCodecContext *dec_ctx = avcodec_alloc_context3(codec);
		avcodec_parameters_to_context(dec_ctx, fmt_ctx->streams[0]->codecpar);
		dec_ctx->thread_count = 1;
		avcodec_open2(dec_ctx, codec, nullptr);

		AVFrame *frame = av_frame_alloc();
		AVPacket *pkt = av_packet_alloc();
		int count = 0;

		fmt_ctx->pb->pos = 0;
		fmt_ctx->pb->buf_ptr = fmt_ctx->pb->buffer;
		avformat_close_input(&fmt_ctx);

		fmt_ctx = avformat_alloc_context();
		fmt_ctx->pb = avio_alloc_context(
			(unsigned char *)g_bitstream_data.data(),
			g_bitstream_data.size(),
			0, nullptr, nullptr, nullptr, nullptr);
		avformat_open_input(&fmt_ctx, "", fmt, nullptr);
		avformat_find_stream_info(fmt_ctx, nullptr);

		while (av_read_frame(fmt_ctx, pkt) >= 0) {
			if (pkt->stream_index == 0) {
				int r = avcodec_send_packet(dec_ctx, pkt);
				while (r >= 0) {
					r = avcodec_receive_frame(dec_ctx, frame);
					if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) break;
					if (r < 0) break;
					count++;
				}
			}
			av_packet_unref(pkt);
		}
		avcodec_send_packet(dec_ctx, nullptr);
		while (avcodec_receive_frame(dec_ctx, frame) >= 0) count++;

		g_stream_frames = count;
		av_frame_free(&frame);
		av_packet_free(&pkt);
		avcodec_free_context(&dec_ctx);
	}

	avformat_close_input(&fmt_ctx);
	return 0;
}

/*
 * Read NAL units using FFmpeg's h264 demuxer.
 */
static bool
read_nal_units(std::vector<nal_unit_t>& nals)
{
	nals.clear();

	AVFormatContext *fmt_ctx = avformat_alloc_context();
	if (!fmt_ctx) return false;

	fmt_ctx->pb = avio_alloc_context(
		(unsigned char *)g_bitstream_data.data(),
		g_bitstream_data.size(),
		0, nullptr, nullptr, nullptr, nullptr);
	if (!fmt_ctx->pb) { avformat_free_context(fmt_ctx); return false; }

	const AVInputFormat *fmt = av_find_input_format("h264");
	if (avformat_open_input(&fmt_ctx, "", fmt, nullptr) < 0) {
		avformat_free_context(fmt_ctx);
		return false;
	}
	avformat_find_stream_info(fmt_ctx, nullptr);

	AVPacket *pkt = av_packet_alloc();
	if (!pkt) {
		avformat_close_input(&fmt_ctx);
		return false;
	}

	while (av_read_frame(fmt_ctx, pkt) >= 0) {
		if (pkt->stream_index == 0 && pkt->size > 0) {
			nal_unit_t nal;
			nal.data.resize(4 + pkt->size);
			nal.data[0] = 0x00;
			nal.data[1] = 0x00;
			nal.data[2] = 0x00;
			nal.data[3] = 0x01;
			memcpy(nal.data.data() + 4, pkt->data, pkt->size);
			nals.push_back(std::move(nal));
		}
		av_packet_unref(pkt);
	}

	av_packet_free(&pkt);
	avformat_close_input(&fmt_ctx);

	LOGI("Read %zu NAL units via FFmpeg demuxer", nals.size());
	return !nals.empty();
}

/* ====================================================================== */
/* Test Case 1: qcap2 Software Decoder                                   */
/* ====================================================================== */

static inst_result_t
swdec_decode_instance(const std::vector<nal_unit_t>& nals, int instance_id)
{
	inst_result_t result = {0, 0.0, false};
	struct timespec start, end;

	LOGI("[SW Instance %d] Starting decode of %zu NAL units", instance_id, nals.size());

	qcap2_event_t* pEvent = qcap2_event_new();
	if (!pEvent) { result.got_error = true; return result; }
	QRESULT qres = qcap2_event_start(pEvent);
	if (qres != QCAP_RS_SUCCESSFUL) { qcap2_event_delete(pEvent); result.got_error = true; return result; }

	qcap2_event_handlers_t* pEventHandlers = qcap2_event_handlers_new();
	if (!pEventHandlers) { qcap2_event_stop(pEvent); qcap2_event_delete(pEvent); result.got_error = true; return result; }
	qres = qcap2_event_handlers_start(pEventHandlers);
	if (qres != QCAP_RS_SUCCESSFUL) { qcap2_event_handlers_delete(pEventHandlers); qcap2_event_stop(pEvent); qcap2_event_delete(pEvent); result.got_error = true; return result; }

	qcap2_video_decoder_t* pVdec = qcap2_video_decoder_new();
	if (!pVdec) { qcap2_event_handlers_stop(pEventHandlers); qcap2_event_handlers_delete(pEventHandlers); qcap2_event_stop(pEvent); qcap2_event_delete(pEvent); result.got_error = true; return result; }

	{
		std::shared_ptr<qcap2_video_encoder_property_t> pProp(
			qcap2_video_encoder_property_new(), qcap2_video_encoder_property_delete);
		qcap2_video_encoder_property_set_type(pProp.get(), QCAP_ENCODER_TYPE_SOFTWARE);
		qcap2_video_encoder_property_set_format(pProp.get(), QCAP_ENCODER_FORMAT_H264);
		qcap2_video_encoder_property_set_resolution(pProp.get(), g_stream_width, g_stream_height);
		qcap2_video_decoder_set_video_property(pVdec, pProp.get());
	}

	qcap2_video_decoder_set_event(pVdec, pEvent);
	qcap2_video_decoder_set_frame_count(pVdec, 64);
	qcap2_video_decoder_set_packet_count(pVdec, (int)nals.size());
	qcap2_video_decoder_set_max_packet_size(pVdec, 1024 * 1024);

	qres = qcap2_video_decoder_start(pVdec);
	if (qres != QCAP_RS_SUCCESSFUL) { qcap2_video_decoder_delete(pVdec); qcap2_event_handlers_stop(pEventHandlers); qcap2_event_handlers_delete(pEventHandlers); qcap2_event_stop(pEvent); qcap2_event_delete(pEvent); result.got_error = true; return result; }

	const int MAX_PACKETS = 64;
	std::vector<qcap2_rcbuffer_t*> packets(MAX_PACKETS);
	for (int i = 0; i < MAX_PACKETS; i++) {
		packets[i] = qcap2_rcbuffer_new_av_packet();
		if (!packets[i]) {
			for (int j = 0; j < i; j++) qcap2_rcbuffer_release(packets[j]);
			qcap2_video_decoder_stop(pVdec); qcap2_video_decoder_delete(pVdec);
			qcap2_event_handlers_stop(pEventHandlers); qcap2_event_handlers_delete(pEventHandlers);
			qcap2_event_stop(pEvent); qcap2_event_delete(pEvent);
			result.got_error = true; return result;
		}
	}

	clock_gettime(CLOCK_MONOTONIC, &start);

	int pushed = 0;
	int packet_idx = 0;
	while (pushed < (int)nals.size()) {
		qcap2_rcbuffer_t* pBuf = packets[packet_idx % MAX_PACKETS];
		packet_idx++;
		qcap2_av_packet_t* pPkt = (qcap2_av_packet_t*)qcap2_rcbuffer_lock_data(pBuf);

		int nSize = (int)nals[pushed].data.size();
		if (!qcap2_av_packet_alloc_buffer(pPkt, nSize)) { qcap2_rcbuffer_unlock_data(pBuf); break; }

		uint8_t* pData; int nDataSize;
		qcap2_av_packet_get_buffer(pPkt, &pData, &nDataSize);
		memcpy(pData, nals[pushed].data.data(), nSize);
		qcap2_av_packet_set_property(pPkt, 0, (pushed == 0) ? TRUE : FALSE);
		qcap2_av_packet_set_pts(pPkt, pushed * 100000LL);
		qcap2_av_packet_set_dts(pPkt, pushed * 100000LL);
		qcap2_rcbuffer_unlock_data(pBuf);

		qres = qcap2_video_decoder_push(pVdec, pBuf);
		if (qres != QCAP_RS_SUCCESSFUL) {
			if (qres == QCAP_RS_ERROR_TIMEOUT) {
				qcap2_rcbuffer_t* pOut = NULL;
				while (qcap2_video_decoder_pop(pVdec, &pOut) == QCAP_RS_SUCCESSFUL) { result.num_frames++; qcap2_rcbuffer_release(pOut); }
				packet_idx--;
				continue;
			}
			break;
		}
		pushed++;
	}

	int pop_tries = 0;
	while (pop_tries < 100) {
		qcap2_rcbuffer_t* pOut = NULL;
		qres = qcap2_video_decoder_pop(pVdec, &pOut);
		if (qres == QCAP_RS_SUCCESSFUL) { result.num_frames++; qcap2_rcbuffer_release(pOut); pop_tries = 0; }
		else { pop_tries++; struct timespec ts = {0, 1000000}; nanosleep(&ts, NULL); }
	}

	clock_gettime(CLOCK_MONOTONIC, &end);
	result.decode_ms = (end.tv_sec - start.tv_sec) * 1000.0 + (end.tv_nsec - start.tv_nsec) / 1000000.0;

	LOGI("[SW Instance %d] Decoded %d frames in %.1f ms", instance_id, result.num_frames, result.decode_ms);

	for (int i = 0; i < MAX_PACKETS; i++) if (packets[i]) qcap2_rcbuffer_release(packets[i]);
	qcap2_video_decoder_stop(pVdec); qcap2_video_decoder_delete(pVdec);
	qcap2_event_handlers_stop(pEventHandlers); qcap2_event_handlers_delete(pEventHandlers);
	qcap2_event_stop(pEvent); qcap2_event_delete(pEvent);

	return result;
}

/* ====================================================================== */
/* Test Case 2: Jetson Multimedia API (NvVideoDecoder / NVDEC)           */
/* ====================================================================== */

#define NVDEC_CHUNK_SIZE    4000000
#define NVDEC_MAX_PLANES    3
#define NVDEC_MAX_DMABUF    32

struct nvdec_cap_ctx {
	NvVideoDecoder    *dec;
	const char        *name;
	int                dmabuff_fd[NVDEC_MAX_DMABUF];
	int                num_cap_buffers;
	int                width;
	int                height;
	volatile bool      got_error;
	volatile bool      got_eos;
	volatile int       frame_count;
	sem_t              cap_sema;
};

static void
nvdec_destroy_dmabuf(nvdec_cap_ctx *ctx)
{
	for (int i = 0; i < ctx->num_cap_buffers; i++) {
		if (ctx->dmabuff_fd[i] != 0) { NvBufSurf::NvDestroy(ctx->dmabuff_fd[i]); ctx->dmabuff_fd[i] = 0; }
	}
	ctx->num_cap_buffers = 0;
}

static int
nvdec_setup_capture(nvdec_cap_ctx *ctx)
{
	NvVideoDecoder *dec = ctx->dec;
	struct v4l2_format format;
	struct v4l2_crop crop;
	int32_t min_buffers;
	int ret;

	ret = dec->capture_plane.getFormat(format);
	if (ret < 0) { LOGE("[%s] getFormat failed", ctx->name); return -1; }
	ret = dec->capture_plane.getCrop(crop);
	if (ret < 0) { LOGE("[%s] getCrop failed", ctx->name); return -1; }

	ctx->width = crop.c.width;
	ctx->height = crop.c.height;

	dec->capture_plane.deinitPlane();
	nvdec_destroy_dmabuf(ctx);

	ret = dec->setCapturePlaneFormat(format.fmt.pix_mp.pixelformat,
		format.fmt.pix_mp.width, format.fmt.pix_mp.height);
	if (ret < 0) { LOGE("[%s] setCapturePlaneFormat failed", ctx->name); return -1; }

	ret = dec->getMinimumCapturePlaneBuffers(min_buffers);
	if (ret < 0) { LOGE("[%s] getMinimumCapturePlaneBuffers failed", ctx->name); return -1; }

	NvBufSurf::NvCommonAllocateParams capParams;
	memset(&capParams, 0, sizeof(capParams));
	if (dec->isGPUEnabled()) {
		capParams.memType = NVBUF_MEM_CUDA_DEVICE;
		capParams.layout = NVBUF_LAYOUT_PITCH;
	} else {
		capParams.memType = NVBUF_MEM_SURFACE_ARRAY;
		capParams.layout = NVBUF_LAYOUT_BLOCK_LINEAR;
	}
	capParams.width = ctx->width;
	capParams.height = ctx->height;
	capParams.memtag = NvBufSurfaceTag_VIDEO_DEC;
	capParams.colorFormat = NVBUF_COLOR_FORMAT_NV12;

	ctx->num_cap_buffers = min_buffers + 1;
	if (ctx->num_cap_buffers > NVDEC_MAX_DMABUF) ctx->num_cap_buffers = NVDEC_MAX_DMABUF;

	ret = NvBufSurf::NvAllocate(&capParams, ctx->num_cap_buffers, ctx->dmabuff_fd);
	if (ret < 0) { LOGE("[%s] NvAllocate failed", ctx->name); return -1; }

	ret = dec->capture_plane.reqbufs(V4L2_MEMORY_DMABUF, ctx->num_cap_buffers);
	if (ret < 0) { LOGE("[%s] reqbufs failed", ctx->name); return -1; }

	ret = dec->capture_plane.setStreamStatus(true);
	if (ret < 0) { LOGE("[%s] capture streamon failed", ctx->name); return -1; }

	for (uint32_t i = 0; i < dec->capture_plane.getNumBuffers(); i++) {
		struct v4l2_buffer v4l2_buf;
		struct v4l2_plane planes[NVDEC_MAX_PLANES];
		memset(&v4l2_buf, 0, sizeof(v4l2_buf));
		memset(planes, 0, sizeof(planes));
		v4l2_buf.index = i;
		v4l2_buf.m.planes = planes;
		v4l2_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		v4l2_buf.memory = V4L2_MEMORY_DMABUF;
		v4l2_buf.m.planes[0].m.fd = ctx->dmabuff_fd[i];
		ret = dec->capture_plane.qBuffer(v4l2_buf, NULL);
		if (ret < 0) { LOGE("[%s] queue cap buf %d failed", ctx->name, i); return -1; }
	}
	return 0;
}

static void
nvdec_capture_thread(nvdec_cap_ctx *ctx)
{
	NvVideoDecoder *dec = ctx->dec;
	struct v4l2_event ev;
	int ret;

	do {
		ret = dec->dqEvent(ev, 50000);
		if (ret < 0) {
			if (errno == EAGAIN) LOGE("[%s] timeout waiting for res change", ctx->name);
			else LOGE("[%s] dqEvent error: %s", ctx->name, strerror(errno));
			ctx->got_error = true;
			sem_post(&ctx->cap_sema);
			return;
		}
	} while (ev.type != V4L2_EVENT_RESOLUTION_CHANGE && !ctx->got_error);

	if (ctx->got_error) { sem_post(&ctx->cap_sema); return; }

	if (nvdec_setup_capture(ctx) < 0) {
		ctx->got_error = true;
		sem_post(&ctx->cap_sema);
		return;
	}

	sem_post(&ctx->cap_sema);

	/* Capture dequeue loop */
	while (!ctx->got_error && !dec->isInError()) {
		struct v4l2_buffer v4l2_cap_buf;
		struct v4l2_plane cap_planes[NVDEC_MAX_PLANES];
		NvBuffer *cap_buffer = NULL;

		memset(&v4l2_cap_buf, 0, sizeof(v4l2_cap_buf));
		memset(cap_planes, 0, sizeof(cap_planes));
		v4l2_cap_buf.m.planes = cap_planes;

		ret = dec->capture_plane.dqBuffer(v4l2_cap_buf, &cap_buffer, NULL, -1);
		if (ret < 0) {
			if (errno == EAGAIN) { usleep(1000); continue; }
			ctx->got_error = true;
			break;
		}

		if (v4l2_cap_buf.flags & V4L2_BUF_FLAG_LAST) break;

		__sync_fetch_and_add(&ctx->frame_count, 1);

		/* Recycle DMABUF fd */
		v4l2_cap_buf.m.planes[0].m.fd = ctx->dmabuff_fd[v4l2_cap_buf.index];
		ret = dec->capture_plane.qBuffer(v4l2_cap_buf, NULL);
		if (ret < 0) { ctx->got_error = true; break; }
	}
}

struct nvdec_inst_ctx {
	std::vector<uint8_t> bitstream;
	int index;
	inst_result_t result;
};

static void
nvdec_decode_thread(nvdec_inst_ctx *thr_ctx)
{
	std::string name_str = std::string("nvdec") + std::to_string(thr_ctx->index);
	const char *name = name_str.c_str();
	struct timespec start, end;

	NvVideoDecoder *dec = NvVideoDecoder::createVideoDecoder(name_str.c_str());
	if (!dec) { LOGE("[%s] createVideoDecoder failed", name); thr_ctx->result.got_error = true; return; }

	dec->enableProfiling();
	if (dec->setOutputPlaneFormat(V4L2_PIX_FMT_H264, NVDEC_CHUNK_SIZE) < 0) {
		LOGE("[%s] setOutputPlaneFormat failed", name); thr_ctx->result.got_error = true; delete dec; return;
	}
	if (dec->isGPUEnabled()) { dec->setGPUId(0); dec->setCudaMemType(V4L2_CUDA_MEM_TYPE_DEVICE); }
	if (dec->subscribeEvent(V4L2_EVENT_RESOLUTION_CHANGE, 0, 0) < 0) {
		LOGE("[%s] subscribeEvent failed", name); thr_ctx->result.got_error = true; delete dec; return;
	}
	dec->setFrameInputMode(1);
	dec->setMaxPerfMode(1);

	if (dec->output_plane.setupPlane(V4L2_MEMORY_MMAP, 4, true, false) < 0) {
		LOGE("[%s] output plane setup failed", name); thr_ctx->result.got_error = true; delete dec; return;
	}
	if (dec->output_plane.setStreamStatus(true) < 0) {
		LOGE("[%s] output streamon failed", name); thr_ctx->result.got_error = true; delete dec; return;
	}

	nvdec_cap_ctx cap_ctx;
	memset(&cap_ctx, 0, sizeof(cap_ctx));
	cap_ctx.dec = dec;
	cap_ctx.name = name;
	sem_init(&cap_ctx.cap_sema, 0, 0);

	/* Queue initial output buffers */
	size_t offset = 0;
	for (uint32_t i = 0; i < dec->output_plane.getNumBuffers(); i++) {
		struct v4l2_buffer v4l2_buf;
		struct v4l2_plane planes[NVDEC_MAX_PLANES];
		NvBuffer *buffer;
		memset(&v4l2_buf, 0, sizeof(v4l2_buf));
		memset(planes, 0, sizeof(planes));
		v4l2_buf.m.planes = planes;
		v4l2_buf.index = i;
		v4l2_buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
		v4l2_buf.memory = V4L2_MEMORY_MMAP;

		buffer = dec->output_plane.getNthBuffer(i);
		if (!buffer) { thr_ctx->result.got_error = true; goto cleanup; }

		size_t remaining = thr_ctx->bitstream.size() - offset;
		if (remaining == 0) break;
		size_t to_copy = std::min((size_t)NVDEC_CHUNK_SIZE, std::min(remaining, (size_t)buffer->planes[0].length));
		memcpy(buffer->planes[0].data, thr_ctx->bitstream.data() + offset, to_copy);
		buffer->planes[0].bytesused = to_copy;
		v4l2_buf.m.planes[0].bytesused = to_copy;
		offset += to_copy;

		if (dec->output_plane.qBuffer(v4l2_buf, NULL) < 0) { thr_ctx->result.got_error = true; goto cleanup; }
	}

	/* Launch capture thread */
	pthread_t cap_thread;
	pthread_create(&cap_thread, NULL, (void *(*)(void *))nvdec_capture_thread, &cap_ctx);
	sem_wait(&cap_ctx.cap_sema);

	if (cap_ctx.got_error) { thr_ctx->result.got_error = true; goto join_cap; }

	clock_gettime(CLOCK_MONOTONIC, &start);

	/* Feed remaining bitstream */
	{
		bool eos = false;
		while (!eos && !cap_ctx.got_error && !dec->isInError()) {
			struct v4l2_buffer v4l2_out_buf;
			struct v4l2_plane out_planes[NVDEC_MAX_PLANES];
			NvBuffer *out_buffer = NULL;
			memset(&v4l2_out_buf, 0, sizeof(v4l2_out_buf));
			memset(out_planes, 0, sizeof(out_planes));
			v4l2_out_buf.m.planes = out_planes;

			if (dec->output_plane.dqBuffer(v4l2_out_buf, &out_buffer, NULL, -1) < 0) {
				cap_ctx.got_error = true; break;
			}

			size_t remaining = thr_ctx->bitstream.size() - offset;
			if (remaining == 0) {
				out_buffer->planes[0].bytesused = 0;
				v4l2_out_buf.m.planes[0].bytesused = 0;
				eos = true;
			} else {
				size_t to_copy = std::min((size_t)NVDEC_CHUNK_SIZE, std::min(remaining, (size_t)out_buffer->planes[0].length));
				memcpy(out_buffer->planes[0].data, thr_ctx->bitstream.data() + offset, to_copy);
				out_buffer->planes[0].bytesused = to_copy;
				v4l2_out_buf.m.planes[0].bytesused = to_copy;
				offset += to_copy;
			}

			if (dec->output_plane.qBuffer(v4l2_out_buf, NULL) < 0) { cap_ctx.got_error = true; break; }
		}
	}

join_cap:
	pthread_join(cap_thread, NULL);

	clock_gettime(CLOCK_MONOTONIC, &end);

	thr_ctx->result.num_frames = cap_ctx.frame_count;
	thr_ctx->result.got_error = cap_ctx.got_error;
	thr_ctx->result.decode_ms = (end.tv_sec - start.tv_sec) * 1000.0 + (end.tv_nsec - start.tv_nsec) / 1000000.0;

	LOGI("[%s] Decoded %d frames in %.1f ms", name, thr_ctx->result.num_frames, thr_ctx->result.decode_ms);

cleanup:
	dec->capture_plane.deinitPlane();
	dec->output_plane.deinitPlane();
	nvdec_destroy_dmabuf(&cap_ctx);
	sem_destroy(&cap_ctx.cap_sema);
	delete dec;
}

/* ====================================================================== */
/* Application                                                           */
/* ====================================================================== */

struct App0 {
	typedef App0 self_t;

	static int Main() {
		self_t app;
		return app.Run();
	}

	bool running;

	int Run() {
		int TEST_CASE;
		ZZ_ENV_INIT(TEST_CASE, 0);

		if (TEST_CASE != 0) {
			DoCase(TEST_CASE);
			return 0;
		}

		running = true;
		LOGI("Press 'q' to exit.");
		LOGI("  1 - Benchmark qcap2 Software Decoder");
		LOGI("  2 - Benchmark Jetson NVDEC Hardware Decoder");
		while (running) {
			char ch;
			int fd_stdin = 0;
			fd_set readfds;
			FD_ZERO(&readfds);
			int fd_max = -1;
			if (fd_stdin > fd_max) fd_max = fd_stdin;
			FD_SET(fd_stdin, &readfds);

			int err = select(fd_max + 1, &readfds, NULL, NULL, NULL);
			if (err < 0) { LOGE("select() failed! err=%d", err); break; }

			if (FD_ISSET(fd_stdin, &readfds)) {
				ch = getchar();
				switch (ch) {
				case 'q': running = false; break;
				case '0': case '1': case '2': case '3': case '4':
				case '5': case '6': case '7': case '8': case '9':
					DoCase(ch - '0'); break;
				}
			}
		}
		return 0;
	}

	void DoCase(int t) {
		LOGD("DoCase(%d)", t);
		switch (t) {
		case 1: mTestCase1.DoWork(); break;
		case 2: mTestCase2.DoWork(); break;
		}
	}

	static const char* GetSamplePath() {
		const char* pEnv = getenv("SWDEC_BENCH_SAMPLE");
		if (pEnv && pEnv[0]) return pEnv;
		return "/usr/src/jetson_multimedia_api/data/Video/sample_outdoor_car_1080p_10fps.h264";
	}

	/* ---- Test Case 1: qcap2 Software Decoder ---- */
	struct TestCase1 : public TestCase {
		typedef TestCase1 self_t;
		typedef TestCase super_t;

		void DoWork() {
			const char* in_file = GetSamplePath();
			int num_instances = 1;
			const char* env_inst = getenv("SWDEC_INSTANCES");
			if (env_inst) num_instances = atoi(env_inst);
			if (num_instances < 1) num_instances = 1;

			LOGI("=== qcap2 Software Decode Benchmark ===");
			LOGI("Input: %s", in_file);
			LOGI("Instances: %d", num_instances);

			/* Load bitstream */
			std::ifstream ifs(in_file, std::ios::binary | std::ios::ate);
			if (!ifs.is_open()) { LOGE("Failed to open %s", in_file); return; }
			std::streamsize fsize = ifs.tellg();
			ifs.seekg(0, std::ios::beg);
			g_bitstream_data.resize(fsize);
			ifs.read((char*)g_bitstream_data.data(), fsize);
			ifs.close();
			LOGI("Loaded %lld bytes", (long long)fsize);

			/* Probe with FFmpeg */
			if (probe_stream() < 0) { LOGE("probe_stream failed"); return; }
			LOGI("Resolution: %dx%d, frames: %d", g_stream_width, g_stream_height, g_stream_frames);

			/* Read NAL units */
			std::vector<nal_unit_t> nals;
			if (!read_nal_units(nals)) { LOGE("read_nal_units failed"); return; }

			/* Run benchmark */
			double total_wall = 0.0;
			long long total_frames = 0;
			int success_runs = 0;

			for (int iter = 0; iter < 3; iter++) {
				std::vector<inst_result_t> results(num_instances);
				std::vector<std::thread> threads;

				struct timespec t0, t1;
				clock_gettime(CLOCK_MONOTONIC, &t0);

				for (int i = 0; i < num_instances; i++) {
					threads.emplace_back([&nals, i, &results]() {
						results[i] = swdec_decode_instance(nals, i);
					});
				}
				for (auto& t : threads) t.join();

				clock_gettime(CLOCK_MONOTONIC, &t1);
				double wall_ms = (t1.tv_sec - t0.tv_sec) * 1000.0 + (t1.tv_nsec - t0.tv_nsec) / 1000000.0;

				int ok = 0, frames = 0;
				for (int i = 0; i < num_instances; i++) {
					if (results[i].num_frames > 0) { ok++; frames += results[i].num_frames; }
				}

				LOGI("  Iter %d: wall=%.0f ms, frames=%d, ok=%d/%d, fps=%.1f",
					iter, wall_ms, frames, ok, num_instances, frames * 1000.0 / wall_ms);

				if (ok > 0) { total_wall += wall_ms; total_frames += frames; success_runs++; }
			}

			if (success_runs > 0) {
				double avg_fps = total_frames * 1000.0 / total_wall;
				LOGI("--- SW Summary ---");
				LOGI("  Avg aggregate FPS:  %.1f", avg_fps);
				LOGI("  Pixel throughput:   ~%d MP/s", (int)(avg_fps * g_stream_width * g_stream_height / 1000000));
			}
		}
	} mTestCase1;

	/* ---- Test Case 2: Jetson NVDEC Hardware Decoder ---- */
	struct TestCase2 : public TestCase {
		typedef TestCase2 self_t;
		typedef TestCase super_t;

		void DoWork() {
			const char* in_file = GetSamplePath();
			int num_instances = 1;
			const char* env_inst = getenv("NVDEC_INSTANCES");
			if (env_inst) num_instances = atoi(env_inst);
			if (num_instances < 1) num_instances = 1;

			LOGI("=== Jetson NVDEC Decode Benchmark ===");
			LOGI("Input: %s", in_file);
			LOGI("Instances: %d", num_instances);

			/* Load bitstream */
			std::ifstream ifs(in_file, std::ios::binary | std::ios::ate);
			if (!ifs.is_open()) { LOGE("Failed to open %s", in_file); return; }
			std::streamsize fsize = ifs.tellg();
			ifs.seekg(0, std::ios::beg);
			g_bitstream_data.resize(fsize);
			ifs.read((char*)g_bitstream_data.data(), fsize);
			ifs.close();
			LOGI("Loaded %lld bytes", (long long)fsize);

			/* Run benchmark */
			double total_wall = 0.0;
			long long total_frames = 0;
			int success_runs = 0;

			for (int iter = 0; iter < 3; iter++) {
				std::vector<nvdec_inst_ctx> contexts(num_instances);
				for (int i = 0; i < num_instances; i++) {
					contexts[i].bitstream = g_bitstream_data;
					contexts[i].index = i;
					contexts[i].result = {0, 0.0, false};
				}

				std::vector<std::thread> threads;
				struct timespec t0, t1;
				clock_gettime(CLOCK_MONOTONIC, &t0);

				for (int i = 0; i < num_instances; i++) {
					threads.emplace_back(nvdec_decode_thread, &contexts[i]);
				}
				for (auto& t : threads) t.join();

				clock_gettime(CLOCK_MONOTONIC, &t1);
				double wall_ms = (t1.tv_sec - t0.tv_sec) * 1000.0 + (t1.tv_nsec - t0.tv_nsec) / 1000000.0;

				int ok = 0, frames = 0;
				for (int i = 0; i < num_instances; i++) {
					if (contexts[i].result.num_frames > 0) { ok++; frames += contexts[i].result.num_frames; }
				}

				LOGI("  Iter %d: wall=%.0f ms, frames=%d, ok=%d/%d, fps=%.1f",
					iter, wall_ms, frames, ok, num_instances, frames * 1000.0 / wall_ms);

				if (ok > 0) { total_wall += wall_ms; total_frames += frames; success_runs++; }
			}

			if (success_runs > 0) {
				double avg_fps = total_frames * 1000.0 / total_wall;
				LOGI("--- NVDEC Summary ---");
				LOGI("  Avg aggregate FPS:  %.1f", avg_fps);
				LOGI("  Pixel throughput:   ~%d MP/s", (int)(avg_fps * 1920 * 1080 / 1000000));
			}
		}
	} mTestCase2;
};

int main(int argc, char* argv[]) {
	g_argc = argc;
	g_argv = argv;

	int ret;
	switch(1) { case 1:
		modules_init_t _modules_init_;
		ret = App0::Main();
	}
	return ret;
}
