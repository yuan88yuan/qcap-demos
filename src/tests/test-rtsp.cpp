/*
 * test-rtsp.cpp - RTSP demuxer test bench with decoder integration
 *
 * Connects to an RTSP URL using QCAP2_DEMUXER_TYPE_RTSP, decodes the
 * audio/video streams with qcap2 decoders (NVMMAPI for video), and
 * reports bitrate/PTS statistics for both encoded and decoded streams.
 *
 * Usage: ./test-rtsp rtsp://<host>:<port>/<path> [options]
 * Options:
 *   -t          Use TCP transport (default: UDP)
 *   -m          Use multicast
 *   -b <ms>     Max buffer length in ms (default: 2000)
 *
 * Env: RTSP_DURATION=<seconds>  streaming time (default 10)
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <vector>
#include <string>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <functional>
#include <memory>
#include <cmath>
#include <cstring>
#include <iomanip>

#include "qcap2.h"
#include "qcap2.v4l2.h"

#include "ZzLog.h"
#include "ZzModules.h"
#include "ZzClock.h"
#include "ZzUtils.h"
#include "testkit.h"

ZZ_INIT_LOG("test-rtsp");

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

namespace __test_rtsp__ {
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

using namespace __test_rtsp__;

/* RTSP configuration (set from command-line args) */
struct RtspConfig {
	std::string url;
	bool use_tcp = false;
	bool multicast = false;
	int max_buffer_ms = 2000;
};

static RtspConfig g_config;

/*
 * Bitrate statistics tracker.
 */
struct BitrateTracker {
	int64_t total_bytes = 0;
	int64_t total_packets = 0;
	int64_t first_pts = 0;
	int64_t last_pts = 0;
	int64_t first_time = 0;
	int64_t last_time = 0;
	int64_t last_report_time = 0;

	void reset() {
		total_bytes = 0;
		total_packets = 0;
		first_pts = 0;
		last_pts = 0;
		first_time = 0;
		last_time = 0;
		last_report_time = 0;
	}

	void add_packet(int64_t pts, int64_t dts, int size, int64_t now) {
		if (total_packets == 0) {
			first_pts = pts;
			first_time = now;
		}
		last_pts = pts;
		last_time = now;
		total_bytes += size;
		total_packets++;
	}

	double duration_sec() const {
		if (total_packets < 2) return 0.0;
		int64_t diff = last_pts - first_pts;
		if (diff > 0) return diff / 1000000.0;
		return (last_time - first_time) / 1000000.0;
	}

	double bitrate_bps() const {
		double sec = duration_sec();
		if (sec <= 0) return 0.0;
		return total_bytes * 8.0 / sec;
	}

	double bitrate_kbps() const {
		return bitrate_bps() / 1000.0;
	}

	void print_report(const char* label, int64_t now) {
		double sec = duration_sec();
		if (sec <= 0) return;

		double kbps = total_bytes * 8.0 / sec / 1000.0;
		double fps = total_packets / sec;

		LOGI("%s: %lld packets, %lld bytes, %.1f sec, %.1f kbps, %.1f fps",
			label,
			(long long)total_packets,
			(long long)total_bytes,
			sec,
			kbps,
			fps);
	}
};

static const char* EncoderTypeName(ULONG type) {
	switch (type) {
	case QCAP_ENCODER_TYPE_SOFTWARE: return "SOFTWARE";
	case QCAP_ENCODER_TYPE_HARDWARE: return "HARDWARE";
	case QCAP_ENCODER_TYPE_INTEL_MEDIA_SDK: return "INTEL_MEDIA_SDK";
	case QCAP_ENCODER_TYPE_AMD_VCE: return "AMD_VCE";
	case QCAP_ENCODER_TYPE_NVIDIA_CUDA: return "NVIDIA_CUDA";
	case QCAP_ENCODER_TYPE_NVIDIA_NVENC: return "NVIDIA_NVENC";
	case QCAP_ENCODER_TYPE_ZZNVCODEC: return "ZZNVCODEC";
	case QCAP_ENCODER_TYPE_GST_SOFTWARE: return "GST_SOFTWARE";
	case QCAP_ENCODER_TYPE_GST_VAAPI: return "GST_VAAPI";
	case QCAP_ENCODER_TYPE_GST_NVENC: return "GST_NVENC";
	case QCAP_ENCODER_TYPE_GST_XLNK: return "GST_XLNK";
	case QCAP_ENCODER_TYPE_CCIMS: return "CCIMS";
	case QCAP_ENCODER_TYPE_NDICODEC: return "NDICODEC";
	case QCAP_ENCODER_TYPE_HIMPP: return "HIMPP";
	case QCAP_ENCODER_TYPE_RKMPP: return "RKMPP";
	case QCAP_ENCODER_TYPE_ALLEGRO: return "ALLEGRO";
	case QCAP_ENCODER_TYPE_VAAPI: return "VAAPI";
	case QCAP_ENCODER_TYPE_EXTERNAL: return "EXTERNAL";
	case QCAP_ENCODER_TYPE_V4L2SRC: return "V4L2SRC";
	case QCAP_ENCODER_TYPE_V4L2NVCODEC: return "V4L2NVCODEC";
	case QCAP_ENCODER_TYPE_NVT_HDAL: return "NVT_HDAL";
	case QCAP_ENCODER_TYPE_ZZNVCODEC2: return "ZZNVCODEC2";
	case QCAP_ENCODER_TYPE_ALLEGRO2: return "ALLEGRO2";
	case QCAP_ENCODER_TYPE_NVMMAPI: return "NVMMAPI";
	case QCAP_ENCODER_TYPE_UNDEFINED: return "UNDEFINED";
	default: return "UNKNOWN";
	}
}

static const char* VideoEncoderFormatName(ULONG format) {
	switch (format) {
	case QCAP_ENCODER_FORMAT_H264: return "H264";
	case QCAP_ENCODER_FORMAT_MPEG2: return "MPEG2";
	case QCAP_ENCODER_FORMAT_H264_3D: return "H264_3D";
	case QCAP_ENCODER_FORMAT_H264_VC: return "H264_VC";
	case QCAP_ENCODER_FORMAT_RAW: return "RAW";
	case QCAP_ENCODER_FORMAT_H265: return "H265";
	case QCAP_ENCODER_FORMAT_AV1: return "AV1";
	case QCAP_ENCODER_FORMAT_RAW_YUY2: return "RAW_YUY2";
	case QCAP_ENCODER_FORMAT_RAW_UYVY: return "RAW_UYVY";
	case QCAP_ENCODER_FORMAT_RAW_YV12: return "RAW_YV12";
	case QCAP_ENCODER_FORMAT_RAW_I420: return "RAW_I420";
	case QCAP_ENCODER_FORMAT_RAW_NV12: return "RAW_NV12";
	case QCAP_ENCODER_FORMAT_RAW_Y800: return "RAW_Y800";
	case QCAP_ENCODER_FORMAT_RAW_YV24: return "RAW_YV24";
	case QCAP_ENCODER_FORMAT_RAW_Y416: return "RAW_Y416";
	case QCAP_ENCODER_FORMAT_RAW_P210: return "RAW_P210";
	case QCAP_ENCODER_FORMAT_RAW_P010: return "RAW_P010";
	case QCAP_ENCODER_FORMAT_RAW_BGR24: return "RAW_BGR24";
	case QCAP_ENCODER_FORMAT_RAW_BGR32: return "RAW_BGR32";
	default: return "UNKNOWN";
	}
}

static const char* AudioEncoderFormatName(ULONG format) {
	switch (format) {
	case QCAP_ENCODER_FORMAT_PCM: return "PCM";
	case QCAP_ENCODER_FORMAT_AAC: return "AAC";
	case QCAP_ENCODER_FORMAT_AAC_ADTS: return "AAC_ADTS";
	case QCAP_ENCODER_FORMAT_MP2: return "MP2";
	case QCAP_ENCODER_FORMAT_MP3: return "MP3";
	case QCAP_ENCODER_FORMAT_MJPEG: return "MJPEG";
	case QCAP_ENCODER_FORMAT_JPEG: return "JPEG";
	case QCAP_ENCODER_FORMAT_PCM_BE: return "PCM_BE";
	case QCAP_ENCODER_FORMAT_S302M: return "S302M";
	case QCAP_ENCODER_FORMAT_SHQ0: return "SHQ0";
	case QCAP_ENCODER_FORMAT_SHQ2: return "SHQ2";
	case QCAP_ENCODER_FORMAT_PCM_FLT: return "PCM_FLT";
	case QCAP_ENCODER_FORMAT_UNDEFINED: return "UNDEFINED";
	default: return "UNKNOWN";
	}
}

static void PrintExtraData(const char* label, const uint8_t* data, int size) {
	std::ostringstream hex;
	const int shown = std::min(size, 256);
	for (int i = 0; i < shown; ++i) {
		if (i) hex << ' ';
		hex << std::hex << std::setw(2) << std::setfill('0') << (unsigned)data[i];
	}
	LOGI("  %s extra data: %d bytes%s%s", label, size,
		(size > shown) ? " (first 256): " : ": ", hex.str().c_str());
}

/* Helper: estimate decoded video frame size from colorspace + resolution */
static int DecodedFrameSize(ULONG nColorSpace, int nWidth, int nHeight) {
	switch (nColorSpace) {
	case QCAP_COLORSPACE_TYPE_NV12:  return nWidth * nHeight * 3 / 2;
	case QCAP_COLORSPACE_TYPE_I420:  return nWidth * nHeight * 3 / 2;
	case QCAP_COLORSPACE_TYPE_YV12:  return nWidth * nHeight * 3 / 2;
	case QCAP_COLORSPACE_TYPE_NV16:  return nWidth * nHeight * 2;
	case QCAP_COLORSPACE_TYPE_YUY2:  return nWidth * nHeight * 2;
	case QCAP_COLORSPACE_TYPE_UYVY:  return nWidth * nHeight * 2;
	case QCAP_COLORSPACE_TYPE_P010:  return nWidth * nHeight * 3; /* 10-bit 4:2:0 */
	case QCAP_COLORSPACE_TYPE_RGB24: return nWidth * nHeight * 3;
	case QCAP_COLORSPACE_TYPE_BGR24: return nWidth * nHeight * 3;
	case QCAP_COLORSPACE_TYPE_RGBA:
	case QCAP_COLORSPACE_TYPE_ARGB32:
	case QCAP_COLORSPACE_TYPE_ABGR32: return nWidth * nHeight * 4;
	case QCAP_COLORSPACE_TYPE_Y800:  return nWidth * nHeight;
	default:
		/* Fallback: assume NV12-like */
		return nWidth * nHeight * 3 / 2;
	}
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

	int Run() {
		int TEST_CASE;
		ZZ_ENV_INIT(TEST_CASE, 1);

		DoCase(TEST_CASE);
		return 0;
	}

	void DoCase(int t) {
		LOGD("DoCase(%d)", t);
		switch (t) {
		case 1: mTestCase1.DoWork(); break;
		default: LOGE("Unknown test case: %d", t); break;
		}
	}

	/* ---- Test Case 1: RTSP Demuxer + Decoders ---- */
	struct TestCase1 : public TestCase {
		typedef TestCase1 self_t;
		typedef TestCase super_t;

		/* Demuxer infrastructure */
		free_stack_t _FreeStack_dmx_;
		free_stack_t _FreeStack_venc_;
		free_stack_t _FreeStack_aenc_;

		/* Encoder handles (populated by OnDemuxer) */
		qcap2_video_encoder_t* mpVenc = NULL;
		qcap2_audio_encoder_t* mpAenc = NULL;
		int mnVencIndex = -1;
		int mnAencIndex = -1;

		/* Decoder handles */
		qcap2_video_decoder_t* mpVdec = NULL;
		qcap2_audio_decoder_t* mpAdec = NULL;
		free_stack_t _FreeStack_vdec_;
		free_stack_t _FreeStack_adec_;

		/* Encoded stream bitrate trackers */
		BitrateTracker mVideoBps;
		BitrateTracker mAudioBps;

		/* Decoded stream bitrate trackers */
		int64_t mnVdecFrames = 0;
		int64_t mnAdecFrames = 0;
		BitrateTracker mVideoDecBps;
		BitrateTracker mAudioDecBps;

		/* Track decoded frame resolution */
		int mnDecWidth = 0, mnDecHeight = 0;
		ULONG mnDecColorSpace = 0;
		int mnVideoExtraShown = -1;
		int mnAudioExtraShown = -1;

		/* Last PTS/DTS for A-V sync diff */
		volatile int64_t mLastVideoPTS = 0;
		volatile int64_t mLastVideoDTS = 0;
		volatile int64_t mLastAudioPTS = 0;
		volatile int64_t mLastAudioDTS = 0;

		/* Stats display interval */
		int64_t mLastReportTime = 0;

		void DoWork() {
			QRESULT qres;

			LOGI("=== RTSP Demuxer Test Bench ===");
			LOGI("URL: %s", g_config.url.c_str());
			LOGI("Transport: %s", g_config.use_tcp ? "TCP" : "UDP");
			LOGI("Multicast: %s", g_config.multicast ? "yes" : "no");
			LOGI("Max buffer: %d ms", g_config.max_buffer_ms);

			mVideoBps.reset();
			mAudioBps.reset();
			mVideoDecBps.reset();
			mAudioDecBps.reset();
			mLastVideoPTS = 0; mLastVideoDTS = 0;
			mLastAudioPTS = 0; mLastAudioDTS = 0;
			mLastReportTime = 0;
			mnVdecFrames = 0; mnAdecFrames = 0;
			mnDecWidth = 0; mnDecHeight = 0; mnDecColorSpace = 0;
			mnVideoExtraShown = -1; mnAudioExtraShown = -1;

			switch(1) { case 1:
				free_stack_t& _FreeStack_ = _FreeStack_main_;

				qres = StartEventHandlers();
				if (qres != QCAP_RS_SUCCESSFUL) {
					LOGE("StartEventHandlers() failed, qres=%d", qres);
					break;
				}
				ZzUtils::Scoped ZZ_GUARD_NAME([&]() {
					OnExitEventHandlers();
				});

				QRESULT qres_evt = QCAP_RS_SUCCESSFUL;
				qres = ExecInEventHandlers(std::bind(&self_t::OnStart, this,
					std::ref(_FreeStack_evt_), std::ref(qres_evt)));
				if (qres != QCAP_RS_SUCCESSFUL) {
					LOGE("ExecInEventHandlers() failed, qres=%d", qres);
					break;
				}
				if (qres_evt != QCAP_RS_SUCCESSFUL) {
					LOGE("OnStart failed, qres=%d", qres_evt);
					break;
				}

				/* Read test duration from environment (default 10 seconds) */
				int RTSP_DURATION;
				ZZ_ENV_INIT(RTSP_DURATION, 10);

				LOGI("RTSP session started. Streaming for %d seconds...", RTSP_DURATION);
				fflush(stdout);

				int64_t nStartTime = qcap2_get_time();
				while (true) {
					int64_t now = qcap2_get_time();
					if (now - nStartTime >= RTSP_DURATION * 1000000LL)
						break;

					struct timespec ts = { 0, 100 * 1000 * 1000 };
					nanosleep(&ts, NULL);
				}

				LOGI("=== Final Report ===");
				PrintSyncReport(qcap2_get_time());
				LOGI("=== Done ===");
			}

			_FreeStack_main_.flush();
		}

		QRETURN OnStart(free_stack_t& _FreeStack_, QRESULT& qres) {
			switch(1) { case 1:
				StartDmx(qres);
				if (qres != QCAP_RS_SUCCESSFUL) {
					LOGE("StartDmx() failed, qres=%d", qres);
					break;
				}

				/* Resource stacks are separate from the event-handler stack.
				 * Flush them here after event handlers have been removed. */
				_FreeStack_ += [&]() {
					_FreeStack_vdec_.flush();
					_FreeStack_adec_.flush();
					_FreeStack_venc_.flush();
					_FreeStack_aenc_.flush();
					_FreeStack_dmx_.flush();
				};
			}

			return QCAP_RT_OK;
		}

		/* ================================================================ */
		/* Demuxer setup                                                    */
		/* ================================================================ */

		QRETURN StartDmx(QRESULT& qres) {
			free_stack_t& _FreeStack_ = _FreeStack_dmx_;
			_FreeStack_.flush();

			switch(1) { case 1:
				qcap2_event_t* pDmxEvent;
				qcap2_demuxer_t* pDmx;
				qres = StartDmxCtx(_FreeStack_, &pDmx, &pDmxEvent);
				if (qres != QCAP_RS_SUCCESSFUL) {
					LOGE("StartDmxCtx() failed, qres=%d", qres);
					break;
				}

				qres = AddEventHandler(_FreeStack_, pDmxEvent, std::bind(&self_t::OnDemuxer, this, pDmx));
				if (qres != QCAP_RS_SUCCESSFUL) {
					LOGE("AddEventHandler(dmx) failed, qres=%d", qres);
					break;
				}

				/* Start playback */
				qres = qcap2_demuxer_play(pDmx);
				if (qres != QCAP_RS_SUCCESSFUL) {
					LOGE("qcap2_demuxer_play() failed, qres=%d", qres);
					break;
				}

				LOGI("Playback started.");
			}

			return QCAP_RT_OK;
		}

		QRESULT StartDmxCtx(free_stack_t& _FreeStack_, qcap2_demuxer_t** ppDmx, qcap2_event_t** ppDmxEvent) {
			QRESULT qres;

			switch(1) { case 1:
				qcap2_event_t* pEvent;
				qres = NewEvent(_FreeStack_, &pEvent);
				if (qres != QCAP_RS_SUCCESSFUL) {
					LOGE("NewEvent() failed, qres=%d", qres);
					break;
				}

				qcap2_demuxer_t* pDmx = qcap2_demuxer_new();
				_FreeStack_ += [pDmx]() {
					qcap2_demuxer_delete(pDmx);
				};

				qcap2_demuxer_set_type(pDmx, QCAP2_DEMUXER_TYPE_RTSP);
				qcap2_demuxer_set_event(pDmx, pEvent);
				qcap2_demuxer_set_url(pDmx, g_config.url.c_str());
				qcap2_demuxer_set_live_source(pDmx, true);
				qcap2_demuxer_set_tcp(pDmx, g_config.use_tcp);
				qcap2_demuxer_set_multicast(pDmx, g_config.multicast);
				qcap2_demuxer_set_max_buffer_length(pDmx, g_config.max_buffer_ms);

				qres = qcap2_demuxer_start(pDmx);
				if (qres != QCAP_RS_SUCCESSFUL) {
					LOGE("qcap2_demuxer_start() failed, qres=%d", qres);
					break;
				}
				_FreeStack_ += [pDmx]() {
					QRESULT qr = qcap2_demuxer_stop(pDmx);
					if (qr != QCAP_RS_SUCCESSFUL)
						LOGE("qcap2_demuxer_stop() failed, qres=%d", qr);
				};

				*ppDmx = pDmx;
				*ppDmxEvent = pEvent;
			}

			return qres;
		}

		/* ================================================================ */
		/* Demuxer event: discover programs, encoders, and start streaming  */
		/* ================================================================ */

		QRETURN OnDemuxer(qcap2_demuxer_t* pDmx) {
			QRESULT qres;

			switch(1) { case 1:
				free_stack_t& _FreeStack_ = _FreeStack_dmx_;

				qres = qcap2_demuxer_update(pDmx);
				if (qres != QCAP_RS_SUCCESSFUL) {
					LOGE("qcap2_demuxer_update() failed, qres=%d", qres);
					break;
				}

				int nVencCount = qcap2_demuxer_get_video_encoder_count(pDmx);
				int nAencCount = qcap2_demuxer_get_audio_encoder_count(pDmx);
				int nProgCount = qcap2_demuxer_get_program_count(pDmx);

				LOGI("Demuxer update: %d programs, %d video encoders, %d audio encoders",
					nProgCount, nVencCount, nAencCount);

				for (int ip = 0; ip < nProgCount; ip++) {
					qcap2_program_info_t* pProg = qcap2_demuxer_get_program_info(pDmx, ip);
					if (!pProg) continue;

					const char* svc_name = qcap2_program_info_get_metadata(pProg, "service_name");
					const char* svc_prov = qcap2_program_info_get_metadata(pProg, "service_provider");
					int id = qcap2_program_info_get_id(pProg);
					int num = qcap2_program_info_get_number(pProg);
					int nProgV = qcap2_program_info_get_video_encoder_count(pProg);
					int nProgA = qcap2_program_info_get_audio_encoder_count(pProg);

					LOGI("  Program[%d]: id=%d number=%d V=%d A=%d '%s' / '%s'",
						ip, id, num, nProgV, nProgA,
						svc_name ? svc_name : "",
						svc_prov ? svc_prov : "");
				}

				if (nProgCount > 0) {
					qcap2_program_info_t* pProg = qcap2_demuxer_get_program_info(pDmx, 0);
					if (pProg) {
						if (qcap2_program_info_get_video_encoder_count(pProg) > 0)
							mnVencIndex = qcap2_program_info_get_video_encoder_index(pProg, 0);
						if (qcap2_program_info_get_audio_encoder_count(pProg) > 0)
							mnAencIndex = qcap2_program_info_get_audio_encoder_index(pProg, 0);
					}
				}

				if (mnVencIndex < 0 && nVencCount > 0) mnVencIndex = 0;
				if (mnAencIndex < 0 && nAencCount > 0) mnAencIndex = 0;

				LOGI("  Using video encoder index: %d, audio encoder index: %d",
					mnVencIndex, mnAencIndex);

				/* ---- Video encoder ---- */
				if (mnVencIndex >= 0 && mnVencIndex < nVencCount) {
					mpVenc = qcap2_demuxer_get_video_encoder(pDmx, mnVencIndex);
					if (mpVenc) {
						PrintVideoEncoderInfo(mpVenc);
						qres = StartVideoEncoder(_FreeStack_, pDmx, mpVenc);
						if (qres != QCAP_RS_SUCCESSFUL)
							LOGE("StartVideoEncoder() failed, qres=%d", qres);
						else
							StartVideoDecoder();
					}
				}

				/* ---- Audio encoder ---- */
				if (mnAencIndex >= 0 && mnAencIndex < nAencCount) {
					mpAenc = qcap2_demuxer_get_audio_encoder(pDmx, mnAencIndex);
					if (mpAenc) {
						PrintAudioEncoderInfo(mpAenc);
						qres = StartAudioEncoder(_FreeStack_, pDmx, mpAenc);
						if (qres != QCAP_RS_SUCCESSFUL)
							LOGE("StartAudioEncoder() failed, qres=%d", qres);
						else
							StartAudioDecoder();
					}
				}
			}

			return QCAP_RT_OK;
		}

		/* ================================================================ */
		/* Video encoder info                                               */
		/* ================================================================ */

		void PrintVideoEncoderInfo(qcap2_video_encoder_t* pVenc) {
			switch(1) { case 1:
				LOGI("--- Video Encoder Info ---");
				std::shared_ptr<qcap2_video_encoder_property_t> pProp(
					qcap2_video_encoder_property_new(), qcap2_video_encoder_property_delete);

				qcap2_video_encoder_get_video_property(pVenc, pProp.get());

				ULONG nEncoderType, nEncoderFormat, nColorSpaceType;
				ULONG nWidth, nHeight;
				double dFrameRate;
				ULONG nRecordMode, nQuality, nBitRate, nGOP;
				ULONG nAspectRatioX, nAspectRatioY;

				qcap2_video_encoder_property_get_property(pProp.get(),
					&nEncoderType, &nEncoderFormat, &nColorSpaceType,
					&nWidth, &nHeight, &dFrameRate,
					&nRecordMode, &nQuality, &nBitRate, &nGOP,
					&nAspectRatioX, &nAspectRatioY);

				LOGI("  Encoder type:   0x%08X (%s)", nEncoderType, EncoderTypeName(nEncoderType));
				LOGI("  Format:         0x%08X (%s)", nEncoderFormat,
					VideoEncoderFormatName(nEncoderFormat));
				LOGI("  Resolution:     %lu x %lu", (unsigned long)nWidth, (unsigned long)nHeight);
				LOGI("  Frame rate:     %.2f fps", dFrameRate);
				LOGI("  Bitrate:        %lu bps", (unsigned long)nBitRate);
				LOGI("  GOP:            %lu", (unsigned long)nGOP);

				uint8_t* pExtraData = NULL;
				int nExtraDataSize = 0;
				qcap2_video_encoder_get_extra_data(pVenc, &pExtraData, &nExtraDataSize);

				PrintExtraData("Video", pExtraData, nExtraDataSize);
			}
		}

		/* ================================================================ */
		/* Audio encoder info                                               */
		/* ================================================================ */

		void PrintAudioEncoderInfo(qcap2_audio_encoder_t* pAenc) {
			switch(1) { case 1:
				LOGI("--- Audio Encoder Info ---");

				std::shared_ptr<qcap2_audio_encoder_property_t> pProp(
					qcap2_audio_encoder_property_new(), qcap2_audio_encoder_property_delete);

				qcap2_audio_encoder_get_audio_property(pAenc, pProp.get());

				ULONG nEncoderType = 0, nEncoderFormat = 0, nChannels = 0, nBitsPerSample = 0, nSampleFrequency = 0;

				qcap2_audio_encoder_property_get_property(pProp.get(),
					&nEncoderType, &nEncoderFormat,
					&nChannels, &nBitsPerSample, &nSampleFrequency);

				LOGI("  Encoder type:   0x%08X (%s)", nEncoderType, EncoderTypeName(nEncoderType));
				LOGI("  Format:         0x%08X (%s)", nEncoderFormat,
					AudioEncoderFormatName(nEncoderFormat));
				LOGI("  Channels:       %lu", (unsigned long)nChannels);
				LOGI("  Bits/sample:    %lu", (unsigned long)nBitsPerSample);
				LOGI("  Sample rate:    %lu Hz", (unsigned long)nSampleFrequency);

				uint8_t* pExtraData = NULL;
				int nExtraDataSize = 0;
				qcap2_audio_encoder_get_extra_data(pAenc, &pExtraData, &nExtraDataSize);

				PrintExtraData("Audio", pExtraData, nExtraDataSize);
			}
		}

		/* ================================================================ */
		/* Video decoder setup                                              */
		/* ================================================================ */

		void StartVideoDecoder() {
			QRESULT qres;

			LOGI("Starting video decoder (software)...");

			switch(1) { case 1:
				_FreeStack_vdec_.flush();

				mpVdec = qcap2_video_decoder_new();
				if (!mpVdec) { LOGE("qcap2_video_decoder_new() failed"); break; }
				_FreeStack_vdec_ += [this]() {
					qcap2_video_decoder_stop(mpVdec);
					qcap2_video_decoder_delete(mpVdec);
					mpVdec = NULL;
				};

				/* Build video property from encoder info, override type */
				std::shared_ptr<qcap2_video_encoder_property_t> pProp(
					qcap2_video_encoder_property_new(), qcap2_video_encoder_property_delete);

				qcap2_video_encoder_get_video_property(mpVenc, pProp.get());

				ULONG nType, nFmt, nCST;
				ULONG nW, nH;
				double nFR;
				ULONG nRM, nQual, nBR, nGOP;
				ULONG nARX, nARY;
				qcap2_video_encoder_property_get_property(pProp.get(),
					&nType, &nFmt, &nCST, &nW, &nH, &nFR,
					&nRM, &nQual, &nBR, &nGOP, &nARX, &nARY);

				/* Override encoder type to NVMMAPI for HW decode */
				qcap2_video_encoder_property_set_type(pProp.get(), QCAP_ENCODER_TYPE_NVMMAPI);
				/* If codec format is unknown, assume H264 (common RTSP codec) */
				if (nFmt == 0 || nFmt == 0xFFFFFFFF)
					qcap2_video_encoder_property_set_format(pProp.get(), QCAP_ENCODER_FORMAT_H264);
				/* Use a reasonable default resolution if unknown */
				if (nW == 0 || nH == 0) { nW = 1920; nH = 1080; }
				/* RTSP pass-through properties may report 0x0 before playback.
				 * Make the fallback dimensions explicit in the decoder property. */
				qcap2_video_encoder_property_set_resolution(pProp.get(), nW, nH);
				/* The NVMMAPI r38 backend emits NV12 capture frames.  RTSP
				 * pass-through encoders commonly leave this field unset. */
				qcap2_video_encoder_property_set_color_space(pProp.get(), QCAP_COLORSPACE_TYPE_NV12);

				qcap2_video_decoder_set_video_property(mpVdec, pProp.get());

				qcap2_event_t* pEvent = NULL;
				qres = NewEvent(_FreeStack_vdec_, &pEvent);
				if (qres != QCAP_RS_SUCCESSFUL) { LOGE("NewEvent(vdec) failed, qres=%d", qres); break; }
				qcap2_video_decoder_set_event(mpVdec, pEvent);
				qres = AddEventHandler(_FreeStack_main_, pEvent, std::bind(&self_t::OnVideoDecoded, this));
				if (qres != QCAP_RS_SUCCESSFUL) { LOGE("AddEventHandler(vdec) failed, qres=%d", qres); break; }

				/* Pass extra data (SPS/PPS) from encoder to decoder */
				uint8_t* pExtra = NULL;
				int nExtraSize = 0;
				qcap2_video_encoder_get_extra_data(mpVenc, &pExtra, &nExtraSize);
				if (pExtra && nExtraSize > 0)
					qcap2_video_decoder_set_extra_data(mpVdec, pExtra, nExtraSize);

				qcap2_video_decoder_set_frame_count(mpVdec, 8);
				qcap2_video_decoder_set_packet_count(mpVdec, 16);
				qcap2_video_decoder_set_max_packet_size(mpVdec, 1024 * 1024);

				qres = qcap2_video_decoder_start(mpVdec);
				if (qres != QCAP_RS_SUCCESSFUL) {
					LOGE("qcap2_video_decoder_start() failed, qres=%d", qres);
					break;
				}

				LOGI("Video decoder started (software).");
			}
		}

		/* ================================================================ */
		/* Audio decoder setup                                              */
		/* ================================================================ */

		void StartAudioDecoder() {
			QRESULT qres;

			LOGI("Starting audio decoder...");

			switch(1) { case 1:
				_FreeStack_adec_.flush();

				mpAdec = qcap2_audio_decoder_new();
				if (!mpAdec) { LOGE("qcap2_audio_decoder_new() failed"); break; }
				_FreeStack_adec_ += [this]() {
					qcap2_audio_decoder_stop(mpAdec);
					qcap2_audio_decoder_delete(mpAdec);
					mpAdec = NULL;
				};

				std::shared_ptr<qcap2_audio_encoder_property_t> pProp(
					qcap2_audio_encoder_property_new(), qcap2_audio_encoder_property_delete);

				qcap2_audio_encoder_get_audio_property(mpAenc, pProp.get());

				qcap2_audio_decoder_set_audio_property(mpAdec, pProp.get());

				qcap2_event_t* pEvent = NULL;
				qres = NewEvent(_FreeStack_adec_, &pEvent);
				if (qres != QCAP_RS_SUCCESSFUL) { LOGE("NewEvent(adec) failed, qres=%d", qres); break; }
				qcap2_audio_decoder_set_event(mpAdec, pEvent);
				qres = AddEventHandler(_FreeStack_main_, pEvent, std::bind(&self_t::OnAudioDecoded, this));
				if (qres != QCAP_RS_SUCCESSFUL) { LOGE("AddEventHandler(adec) failed, qres=%d", qres); break; }

				uint8_t* pExtra = NULL;
				int nExtraSize = 0;
				qcap2_audio_encoder_get_extra_data(mpAenc, &pExtra, &nExtraSize);
				if (pExtra && nExtraSize > 0)
					qcap2_audio_decoder_set_extra_data(mpAdec, pExtra, nExtraSize);

				qcap2_audio_decoder_set_frame_count(mpAdec, 8);
				qcap2_audio_decoder_set_packet_count(mpAdec, 16);

				qres = qcap2_audio_decoder_start(mpAdec);
				if (qres != QCAP_RS_SUCCESSFUL) {
					LOGE("qcap2_audio_decoder_start() failed, qres=%d", qres);
					break;
				}

				LOGI("Audio decoder started.");
			}
		}

		/* ================================================================ */
		/* Start video encoder (register event callback)                    */
		/* ================================================================ */

		QRESULT StartVideoEncoder(free_stack_t& _FreeStack_, qcap2_demuxer_t* pDmx,
			qcap2_video_encoder_t* pVenc) {
			QRESULT qres;

			switch(1) { case 1:
				qcap2_event_t* pEvent;
				qres = NewEvent(_FreeStack_venc_, &pEvent);
				if (qres != QCAP_RS_SUCCESSFUL) {
					LOGE("NewEvent(venc) failed, qres=%d", qres);
					break;
				}

				qcap2_video_encoder_set_event(pVenc, pEvent);

				qres = qcap2_video_encoder_start(pVenc);
				if (qres != QCAP_RS_SUCCESSFUL) {
					LOGE("qcap2_video_encoder_start() failed, qres=%d", qres);
					break;
				}
				_FreeStack_venc_ += [pVenc]() {
					QRESULT qr = qcap2_video_encoder_stop(pVenc);
					if (qr != QCAP_RS_SUCCESSFUL)
						LOGD("qcap2_video_encoder_stop() qres=%d", qr);
				};

				qres = AddEventHandler(_FreeStack_, pEvent, std::bind(&self_t::OnVideoPacket, this));
				if (qres != QCAP_RS_SUCCESSFUL) {
					LOGE("AddEventHandler(venc) failed, qres=%d", qres);
					break;
				}
			}

			return qres;
		}

		/* ================================================================ */
		/* Start audio encoder (register event callback)                    */
		/* ================================================================ */

		QRESULT StartAudioEncoder(free_stack_t& _FreeStack_, qcap2_demuxer_t* pDmx,
			qcap2_audio_encoder_t* pAenc) {
			QRESULT qres;

			switch(1) { case 1:
				qcap2_event_t* pEvent;
				qres = NewEvent(_FreeStack_aenc_, &pEvent);
				if (qres != QCAP_RS_SUCCESSFUL) {
					LOGE("NewEvent(aenc) failed, qres=%d", qres);
					break;
				}

				qcap2_audio_encoder_set_event(pAenc, pEvent);

				qres = qcap2_audio_encoder_start(pAenc);
				if (qres != QCAP_RS_SUCCESSFUL) {
					LOGE("qcap2_audio_encoder_start() failed, qres=%d", qres);
					break;
				}
				_FreeStack_aenc_ += [pAenc]() {
					QRESULT qr = qcap2_audio_encoder_stop(pAenc);
					if (qr != QCAP_RS_SUCCESSFUL)
						LOGD("qcap2_audio_encoder_stop() qres=%d", qr);
				};

				qres = AddEventHandler(_FreeStack_, pEvent, std::bind(&self_t::OnAudioPacket, this));
				if (qres != QCAP_RS_SUCCESSFUL) {
					LOGE("AddEventHandler(aenc) failed, qres=%d", qres);
					break;
				}
			}

			return qres;
		}

		/* ================================================================ */
		/* Video packet callback: push to decoder, pop decoded frames       */
		/* ================================================================ */

		QRETURN OnVideoPacket() {
			QRETURN qret = QCAP_RT_OK;
			QRESULT qres;

			switch(1) { case 1:
				if (!mpVenc) break;

				qcap2_rcbuffer_t* pRCBuffer_;
				qres = qcap2_video_encoder_pop(mpVenc, &pRCBuffer_);
				if (qres != QCAP_RS_SUCCESSFUL) {
					if (qres != QCAP_RS_ERROR_TIMEOUT)
						LOGE("qcap2_video_encoder_pop() failed, qres=%d", qres);
					break;
				}

				/* Keep the encoder-owned buffer alive while the decoder consumes it. */
				std::shared_ptr<qcap2_rcbuffer_t> pBufferGuard(pRCBuffer_, qcap2_rcbuffer_release);

				/* Read encoded packet info */
				qcap2_av_packet_t* pPkt = (qcap2_av_packet_t*)qcap2_rcbuffer_lock_data(pRCBuffer_);

				int64_t nPTS, nDTS;
				qcap2_av_packet_get_pts(pPkt, &nPTS);
				qcap2_av_packet_get_dts(pPkt, &nDTS);
				uint8_t* pBuf; int nSize;
				qcap2_av_packet_get_buffer(pPkt, &pBuf, &nSize);
				int nStreamIndex; BOOL bIsKeyFrame;
				qcap2_av_packet_get_property(pPkt, &nStreamIndex, &bIsKeyFrame);
				uint8_t* pExtra = NULL; int nExtraSize = 0;
				qcap2_video_encoder_get_extra_data(mpVenc, &pExtra, &nExtraSize);
				if (nExtraSize != mnVideoExtraShown) {
					PrintExtraData("Video", pExtra, nExtraSize);
					mnVideoExtraShown = nExtraSize;
				}

				int64_t now = qcap2_get_time();
				mVideoBps.add_packet(nPTS, nDTS, nSize, now);
				mLastVideoPTS = nPTS;
				mLastVideoDTS = nDTS;

				qcap2_rcbuffer_unlock_data(pRCBuffer_);

				/* Push packet to decoder */
				if (mpVdec) {
					qres = qcap2_video_decoder_push(mpVdec, pRCBuffer_);
					if (qres != QCAP_RS_SUCCESSFUL)
						LOGE("qcap2_video_decoder_push() failed, qres=%d", qres);

				}

				/* Periodic stats */
				if (now - mVideoBps.last_report_time > 5000000LL) {
					PrintSyncReport(now);
					mVideoBps.last_report_time = now;
				}
			}

			return qret;
		}

		/* ================================================================ */
		/* Audio packet callback: push to decoder, pop decoded frames       */
		/* ================================================================ */

		QRETURN OnAudioPacket() {
			QRETURN qret = QCAP_RT_OK;
			QRESULT qres;

			switch(1) { case 1:
				if (!mpAenc) break;

				qcap2_rcbuffer_t* pRCBuffer_;
				qres = qcap2_audio_encoder_pop(mpAenc, &pRCBuffer_);
				if (qres != QCAP_RS_SUCCESSFUL) {
					if (qres != QCAP_RS_ERROR_TIMEOUT)
						LOGE("qcap2_audio_encoder_pop() failed, qres=%d", qres);
					break;
				}
				std::shared_ptr<qcap2_rcbuffer_t> pBufferGuard(pRCBuffer_, qcap2_rcbuffer_release);

				qcap2_av_packet_t* pPkt = (qcap2_av_packet_t*)qcap2_rcbuffer_lock_data(pRCBuffer_);

				int64_t nPTS, nDTS;
				qcap2_av_packet_get_pts(pPkt, &nPTS);
				qcap2_av_packet_get_dts(pPkt, &nDTS);
				uint8_t* pBuf; int nSize;
				qcap2_av_packet_get_buffer(pPkt, &pBuf, &nSize);
				int nStreamIndex; BOOL bIsKeyFrame;
				qcap2_av_packet_get_property(pPkt, &nStreamIndex, &bIsKeyFrame);
				uint8_t* pExtra = NULL; int nExtraSize = 0;
				qcap2_audio_encoder_get_extra_data(mpAenc, &pExtra, &nExtraSize);
				if (nExtraSize != mnAudioExtraShown) {
					PrintExtraData("Audio", pExtra, nExtraSize);
					mnAudioExtraShown = nExtraSize;
				}

				int64_t now = qcap2_get_time();
				mAudioBps.add_packet(nPTS, nDTS, nSize, now);
				mLastAudioPTS = nPTS;
				mLastAudioDTS = nDTS;

				qcap2_rcbuffer_unlock_data(pRCBuffer_);

				/* Push packet to decoder */
				if (mpAdec) {
					qres = qcap2_audio_decoder_push(mpAdec, pRCBuffer_);
					if (qres != QCAP_RS_SUCCESSFUL)
						LOGE("qcap2_audio_decoder_push() failed, qres=%d", qres);

				}

				/* Periodic stats */
				if (now - mAudioBps.last_report_time > 5000000LL) {
					PrintSyncReport(now);
					mAudioBps.last_report_time = now;
				}
			}

			return qret;
		}

		/* Decoder events can arrive after push() returns (NVDEC is async). */
		QRETURN OnVideoDecoded() {
			if (!mpVdec) return QCAP_RT_OK;
			while (true) {
				qcap2_rcbuffer_t* pOut = NULL;
				if (qcap2_video_decoder_pop(mpVdec, &pOut) != QCAP_RS_SUCCESSFUL) break;
				qcap2_av_frame_t* pFrame = (qcap2_av_frame_t*)qcap2_rcbuffer_get_data(pOut);
				if (pFrame) {
					ULONG c, w, h; int64_t pts = 0;
					qcap2_av_frame_get_video_property(pFrame, &c, &w, &h);
					qcap2_av_frame_get_pts(pFrame, &pts);
					mnDecWidth = (int)w; mnDecHeight = (int)h; mnDecColorSpace = c;
					mVideoDecBps.add_packet(pts, pts, DecodedFrameSize(c, (int)w, (int)h), qcap2_get_time());
					mnVdecFrames++;
				}
				qcap2_rcbuffer_release(pOut);
			}
			return QCAP_RT_OK;
		}

		QRETURN OnAudioDecoded() {
			if (!mpAdec) return QCAP_RT_OK;
			while (true) {
				qcap2_rcbuffer_t* pOut = NULL;
				if (qcap2_audio_decoder_pop(mpAdec, &pOut) != QCAP_RS_SUCCESSFUL) break;
				qcap2_av_frame_t* pFrame = (qcap2_av_frame_t*)qcap2_rcbuffer_get_data(pOut);
				if (pFrame) {
					ULONG ch, fmt, rate, size; int64_t pts = 0;
					qcap2_av_frame_get_audio_property(pFrame, &ch, &fmt, &rate, &size);
					qcap2_av_frame_get_pts(pFrame, &pts);
					mAudioDecBps.add_packet(pts, pts, (int)size, qcap2_get_time());
					mnAdecFrames++;
				}
				qcap2_rcbuffer_release(pOut);
			}
			return QCAP_RT_OK;
		}

		/* ================================================================ */
		/* Periodic and final report                                        */
		/* ================================================================ */

		void PrintSyncReport(int64_t now) {
			mVideoBps.print_report("Enc Video", now);
			mAudioBps.print_report("Enc Audio", now);

			LOGI("  Decoded: %lld video frames, %lld audio frames",
				(long long)mnVdecFrames, (long long)mnAdecFrames);

			if (mnDecWidth > 0 && mnDecHeight > 0) {
				LOGI("  Video resolution: %d x %d", mnDecWidth, mnDecHeight);
				mVideoDecBps.print_report("Dec Video", now);
			}
			if (mnAdecFrames > 0)
				mAudioDecBps.print_report("Dec Audio", now);

			if (mLastVideoPTS != 0 && mLastAudioPTS != 0) {
				double diff_pts = (mLastAudioPTS - mLastVideoPTS) / 1000000.0;
				double diff_dts = (mLastAudioDTS - mLastVideoDTS) / 1000000.0;
				if (fabs(diff_pts) < 1.0)
					LOGI("  A-V diff: PTS=%+.0f us, DTS=%+.0f us", diff_pts * 1e6, diff_dts * 1e6);
				else
					LOGI("  A-V diff: PTS=%+.3f s, DTS=%+.3f s", diff_pts, diff_dts);
			}
		}
	} mTestCase1;
};

/* ====================================================================== */
/* Main: parse arguments and launch                                       */
/* ====================================================================== */

static void
print_usage(const char* prog)
{
	std::cerr << "Usage: " << prog << " rtsp://<host>:<port>/<path> [options]" << std::endl;
	std::cerr << "Options:" << std::endl;
	std::cerr << "  -t          Use TCP transport (default: UDP)" << std::endl;
	std::cerr << "  -m          Use multicast" << std::endl;
	std::cerr << "  -b <ms>     Max buffer length in ms (default: 2000)" << std::endl;
}

int main(int argc, char* argv[]) {
	g_argc = argc;
	g_argv = argv;

	if (argc < 2) {
		print_usage(argv[0]);
		return -1;
	}

	g_config.url = argv[1];

	for (int i = 2; i < argc; i++) {
		if (strcmp(argv[i], "-t") == 0) {
			g_config.use_tcp = true;
		} else if (strcmp(argv[i], "-m") == 0) {
			g_config.multicast = true;
		} else if (strcmp(argv[i], "-b") == 0 && i + 1 < argc) {
			g_config.max_buffer_ms = atoi(argv[++i]);
			if (g_config.max_buffer_ms < 100) g_config.max_buffer_ms = 100;
		} else {
			std::cerr << "Unknown option: " << argv[i] << std::endl;
			print_usage(argv[0]);
			return -1;
		}
	}

	int ret;
	switch(1) { case 1:
		modules_init_t _modules_init_;
		ret = App0::Main();
	}

	return ret;
}
