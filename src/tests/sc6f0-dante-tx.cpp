#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <atomic>
#include <string>
#include <vector>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <unistd.h>
#include <linux/netlink.h>
#include <drm.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>

#include "qcap2.h"
#include "qcap2.v4l2.h"
#include "qcap2.allegro2.h"

#include "ZzLog.h"
#include "ZzModules.h"
#include "ZzClock.h"
#include "ZzStats.h"
#include "ZzUtils.h"
#include "testkit.h"

extern "C" {
#include "dauservice.h"
#include "zst_types.h"
#include "zst_buffer.h"
#include "zst_pad.h"
#include "zst_element.h"
#include "zst_pipeline.h"
#include "zst_scheduler.h"
#include "zst_clock.h"
#include "zst_bus.h"
#include "zstreamer/elements/zst_dante_session.h"
#include "zstreamer/elements/zst_dante_video_coordinator.h"
#include "zstreamer/elements/zst_dante_dep_audio.h"
#include "zstreamer/elements/zst_audio_resampler.h"
#include "zstreamer/elements/zst_rtp_payloader.h"
#include "zstreamer/elements/zst_dante_udp_sink.h"
}

ZZ_INIT_LOG("sc6f0-dante-tx");

int g_argc = 0;
char** g_argv = NULL;
static FILE* g_tx_dump = NULL;
static volatile sig_atomic_t g_tx_stop = 0;
static void tx_sig_handler(int) { g_tx_stop = 1; }

static std::string g_tx_direct_rx_ip;
static uint16_t g_tx_direct_port = 16490;
static std::string g_tx_direct_mirror_rx_ip;
static uint16_t g_tx_direct_mirror_port = 16491;

ZZ_MODULE_DECL(__zz_log__);

using namespace __zz_clock__;

namespace __sc6f0_dante_tx__ {
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

	inline float mode_vrefresh(drmModeModeInfo *mode) {
		return mode->clock * 1000.00 / (mode->htotal * mode->vtotal);
	}

	static uint32_t get_prop_id(int fd, drmModeObjectProperties *props, std::shared_ptr<drmModePropertyRes>* prop_res, const char *name) {
		for (uint32_t i = 0; i < props->count_props; i++) {
			std::shared_ptr<drmModePropertyRes> p(drmModeGetProperty(fd, props->props[i]), drmModeFreeProperty);
			if (!strcmp(p->name, name)) {
				if(prop_res) *prop_res = p;
				return p->prop_id;
			}
		}

		return 0;
	}
}

using namespace __sc6f0_dante_tx__;
using __testkit__::wait_for_test_finish;
using __testkit__::TestCase;
using __testkit__::free_stack_t;
using __testkit__::NewEvent;
using __testkit__::AddEventHandler;
using __testkit__::spinlock_lock;
using __testkit__::spinlock_unlock;

struct App0 {
	typedef App0 self_t;

	static int Main() {
		self_t app;
		return app.Run();
	}

	int Run() {
		mTestCase1.DoWork();

		return 0;
	}

	struct zst_dante_tx_t {
		zst_pipeline_t* pipe = nullptr;
		zst_scheduler_t* sched = nullptr;
		zst_clock_t* clock = nullptr;
		zst_element_t* session = nullptr;
		zst_element_t* coordinator = nullptr;
		zst_element_t* tx_dep_sink = nullptr;
		zst_element_t* tx_resampler = nullptr;
		zst_element_t* mirror_rtp_payloader = nullptr;
		zst_element_t* mirror_udp_sink = nullptr;
		zst_pad_t* tx_video_pad = nullptr;
		zst_pad_t* tx_audio_pad = nullptr;
		zst_pad_t* app_video_src_pad = nullptr;
		zst_pad_t* app_video_mirror_src_pad = nullptr;
		zst_pad_t* app_audio_src_pad = nullptr;
		pthread_t bus_th = 0;
		pthread_t sched_th = 0;
		std::atomic<bool> running{false};
		std::vector<uint8_t> h264_parameter_sets;

		bool direct_mode = false;
		std::string direct_rx_ip;
		uint16_t direct_port = 16490;

		uint32_t audio_channels = 2;
		uint32_t audio_sample_rate = 48000;
		uint32_t audio_bits = 16;
		uint32_t video_width = 1920;
		uint32_t video_height = 1080;
		double video_fps = 60.0;

		std::atomic<uint64_t> tx_video_frames{0};
		std::atomic<uint64_t> tx_video_bytes{0};
		std::atomic<uint64_t> tx_audio_frames{0};
		std::atomic<uint64_t> tx_audio_bytes{0};
		std::atomic<uint64_t> tx_audio_samples{0};
		uint64_t start_time_ms = 0;
	};

	struct TestCase1 : public TestCase {
		typedef TestCase1 self_t;
		typedef TestCase super_t;

		free_stack_t _FreeStack_vsrc_;

		int nSnapshot;
		std::atomic<uint64_t> frames_since_idr{0};

		std::atomic_flag current_video_format_spinlock = ATOMIC_FLAG_INIT;
		struct video_format current_video_format;

		std::atomic_flag current_audio_info_spinlock = ATOMIC_FLAG_INIT;
		struct audio_info current_audio_info;

		dau_service* pHdmiRxDauServ;
		bool bDmxStarted;
		bool mHavePublishedVideoState;
		bool mHavePublishedAudioState;

#if BUILD_WITH_DAUSERVICE
		static bool video_format_equal(const struct video_format& lhs,
			const struct video_format& rhs)
		{
			return lhs.width == rhs.width &&
				lhs.height == rhs.height &&
				lhs.framerate == rhs.framerate &&
				lhs.colorformat == rhs.colorformat &&
				lhs.bpp == rhs.bpp &&
				lhs.interlaced == rhs.interlaced &&
				lhs.locked == rhs.locked;
		}

		static bool audio_info_equal(const struct audio_info& lhs,
			const struct audio_info& rhs)
		{
			return lhs.channel_count == rhs.channel_count &&
				lhs.speaker_map == rhs.speaker_map &&
				lhs.level_shift_value == rhs.level_shift_value &&
				lhs.downmix_inhibit == rhs.downmix_inhibit &&
				lhs.lfe_playback_level == rhs.lfe_playback_level &&
				lhs.sample_rate == rhs.sample_rate &&
				lhs.encoding == rhs.encoding &&
				lhs.sample_bits == rhs.sample_bits;
		}

		static void _get_cable_status(struct dau_service *dau,
						 struct DBusMessage *message,
						 void *ctxt)
		{
			self_t* pThis = (self_t*)ctxt;

			pThis->get_cable_status(dau, message);
		}

		void get_cable_status(struct dau_service *dau,
						 struct DBusMessage *message)
		{
			spinlock_lock(current_video_format_spinlock);
			bool locked = current_video_format.locked;
			spinlock_unlock(current_video_format_spinlock);

			dau_reply_get_cable_status(dau, message, locked);

			// StartDmx();
		}

		static void _get_hdcp(struct dau_service *dau,
						struct DBusMessage *message,
						void *ctxt)
		{
			struct hdcp hdcp;

			memset(&hdcp, 0x00, sizeof(hdcp));
			hdcp.current = HDCP_VERSION_22;
			hdcp.hdcp14_supported = false;
			hdcp.hdcp22_supported = true;

			dau_reply_get_hdcp(dau, message, &hdcp);
		}

		static void _set_hdcp(struct dau_service *dau,
						struct DBusMessage *message,
						enum hdcp_version version,
						void *ctxt)
		{
			dau_reply_success(dau, message);
		}

		static void _get_video_format(struct dau_service *dau,
						 struct DBusMessage *message,
						 void *ctxt)
		{
			self_t* pThis = (self_t*)ctxt;

			pThis->get_video_format(dau, message);
		}

		void get_video_format(struct dau_service *dau, struct DBusMessage *message) {
			video_format format;

			spinlock_lock(current_video_format_spinlock);
			format = current_video_format;
			spinlock_unlock(current_video_format_spinlock);

			dau_reply_get_video_format(dau, message, &format);
		}

		static void _get_audio_info(struct dau_service *dau,
						   struct DBusMessage *message,
						   void *ctxt)
		{
			self_t* pThis = (self_t*)ctxt;

			pThis->get_audio_info(dau, message);
		}

		void get_audio_info(struct dau_service *dau, struct DBusMessage *message) {
			audio_info info;

			spinlock_lock(current_audio_info_spinlock);
			info = current_audio_info;
			spinlock_unlock(current_audio_info_spinlock);

			dau_reply_get_audio_info(dau, message, &info);
		}

		static void _get_hdr(struct dau_service *dau,
					struct DBusMessage *message,
					void *ctxt)
		{
			char hdr[HDR_LEN] = { 0x00, 0x01, 0x02 };
			char dv[DV_LEN] = { 0x04, 0x05, 0x06 };
			struct hdr_info info = {
				.avi_c = 1,
				.avi_ec = 2,
				.avi_q = 3,
				.hdr = hdr,
				.dv = dv
			};

			dau_reply_get_hdr(dau, message, &info);
		}

		static void _set_edid(struct dau_service *dau,
						struct DBusMessage *message,
						const void *edid,
						void *ctxt)
		{
			dau_reply_success(dau, message);
		}

		static void _toggle_hpd(struct dau_service *dau,
					   struct DBusMessage *message,
					   void *ctxt)
		{
			dau_reply_success(dau, message);
		}

		static void _set_video_format(struct dau_service *dau,
						 struct DBusMessage *message,
						 const struct video_format *format,
						 void *ctxt)
		{
			dau_reply_success(dau, message);
		}

		static void _set_audio_info(struct dau_service *dau,
						   struct DBusMessage *message,
						   const struct audio_info *info,
						   void *ctxt)
		{
			dau_reply_success(dau, message);
		}

		static void _set_hdr(struct dau_service *dau,
					struct DBusMessage *message,
					const struct hdr_info *hdr,
					void *ctxt)
		{
			dau_reply_success(dau, message);
		}

		static void _get_edid(struct dau_service *dau,
					 struct DBusMessage *message,
					 void *ctxt)
		{
			unsigned char edid[EDID_LEN];
			unsigned int i;

			for (i = 0; i < sizeof(edid); ++i)
				edid[i] = i;

			dau_reply_get_edid(dau, message, edid);
		}
#endif // BUILD_WITH_DAUSERVICE

		void DoWork() {
			QRESULT qres;

			memset(&current_video_format, 0, sizeof(current_video_format));
			memset(&current_audio_info, 0, sizeof(current_audio_info));
			nSnapshot = 0;

			pHdmiRxDauServ = NULL;
			bDmxStarted = false;
			mHavePublishedVideoState = false;
			mHavePublishedAudioState = false;

			switch(1) { case 1:
				free_stack_t& _FreeStack_ = _FreeStack_main_;

				qres = StartEventHandlers();
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): StartEventHandlers() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
				ZzUtils::Scoped ZZ_GUARD_NAME([&]() {
					OnExitEventHandlers();
				} );

				QRESULT qres_evt = QCAP_RS_SUCCESSFUL;
				qres = ExecInEventHandlers(std::bind(&self_t::OnStart, this,
					std::ref(_FreeStack_evt_), std::ref(qres_evt)));
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): ExecInEventHandlers() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
				if(qres_evt != QCAP_RS_SUCCESSFUL) {
					break;
				}

				while (!g_tx_stop) {
					pause();
				}
			}

			_FreeStack_main_.flush();
		}

		QRETURN OnStart(free_stack_t& _FreeStack_, QRESULT& qres) {
			switch(1) { case 1:
				dau_service* pHdmiRxDauServ = NULL;

#if BUILD_WITH_DAUSERVICE
				if (g_tx_direct_rx_ip.empty()) {
					qres = StartDauServ(_FreeStack_, &pHdmiRxDauServ);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): StartDauServ() failed, qres=%d", __FUNCTION__, __LINE__, qres);
						break;
					}
				}
#endif // BUILD_WITH_DAUSERVICE

				this->pHdmiRxDauServ = pHdmiRxDauServ;

				StartDmx();
			}
			return QCAP_RT_OK;
		}

#if BUILD_WITH_DAUSERVICE
		QRESULT StartDauServ(free_stack_t& _FreeStack_, dau_service** ppHdmiRxDauServ) {
			QRESULT qres = QCAP_RS_SUCCESSFUL;

			switch(1) { case 1:
				static const struct dau_service_methods methods = {
					.get_cable_status = _get_cable_status,
					.get_hdcp = _get_hdcp,
					.set_hdcp = _set_hdcp,

					.get_video_format = _get_video_format,
					.get_audio_info = _get_audio_info,
					.get_hdr = _get_hdr,
					.set_edid = _set_edid,
					.toggle_hpd = _toggle_hpd,

					.set_video_format = NULL,
					.set_audio_info = NULL,
					.set_hdr = NULL,
					.get_edid = NULL,
				};

				struct dau_service* hdmirx = dau_service_register(DAU_SERVICE_SOURCE, 0, &methods, this);
				if (! hdmirx) {
					qres = QCAP_RS_ERROR_GENERAL;
					LOGE("%s(%d): dau_service_register() failed", __FUNCTION__, __LINE__);
					break;
				}
				_FreeStack_ += [hdmirx]() {
					dau_service_unregister(hdmirx);
				};

				int fd_hdmirx = dau_service_get_fd(hdmirx);

				qres = AddEventHandler(_FreeStack_, fd_hdmirx, std::bind(&self_t::OnHdmiRxEvent, this, hdmirx));
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): AddEventHandler() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				*ppHdmiRxDauServ = hdmirx;
			}

			return qres;
		}

		QRETURN OnHdmiRxEvent(dau_service* dauserv) {
			dau_service_dispatch(dauserv, 0);
			return QCAP_RT_OK;
		}
#endif // BUILD_WITH_DAUSERVICE

		void StartDmx() {
			QRESULT qres;
			free_stack_t& _FreeStack_ = _FreeStack_evt_;

			if(! bDmxStarted) switch(1) { case 1:
				qcap2_event_t* pDmxEvent;
				qcap2_demuxer_t* pDmx;
				qres = StartDmx(_FreeStack_, &pDmx, &pDmxEvent);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): StartDmx() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				qres = AddEventHandler(_FreeStack_, pDmxEvent, std::bind(&self_t::OnDmx, this, pDmx,
					pHdmiRxDauServ));
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): AddEventHandler() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				_FreeStack_ += [&]() {
					_FreeStack_vsrc_.flush();
				};

				bDmxStarted = true;
			}
		}

		QRESULT StartDmx(free_stack_t& _FreeStack_, qcap2_demuxer_t** ppDmx, qcap2_event_t** ppDmxEvent) {
			QRESULT qres;

			switch(1) { case 1:
				qcap2_event_t* pEvent;
				qres = NewEvent(_FreeStack_, &pEvent);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): NewEvent() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				qcap2_demuxer_t* pDmx = qcap2_demuxer_new();
				_FreeStack_ += [pDmx]() {
					qcap2_demuxer_delete(pDmx);
				};

				qcap2_demuxer_set_type(pDmx, QCAP2_DEMUXER_TYPE_SC6F0);
				qcap2_demuxer_set_event(pDmx, pEvent);

				qres = qcap2_demuxer_start(pDmx);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_demuxer_start() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
				_FreeStack_ += [pDmx]() {
					QRESULT qres;

					qres = qcap2_demuxer_stop(pDmx);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): qcap2_demuxer_stop() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					}
				};

				*ppDmx = pDmx;
				*ppDmxEvent = pEvent;
			}

			return qres;
		}

		QRETURN OnDmx(qcap2_demuxer_t* pDmx, dau_service* pHdmiRxDauServ) {
			QRESULT qres;

			switch(1) {	case 1:
				free_stack_t& _FreeStack_ = _FreeStack_vsrc_;

				// to stop a/v src/codec which are running
				_FreeStack_.flush();

				qres = qcap2_demuxer_update(pDmx);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_demuxer_update() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				LOGD("%d VSRCs, %d ASRCs, %d PROGs",
					qcap2_demuxer_get_video_source_count(pDmx),
					qcap2_demuxer_get_audio_source_count(pDmx),
					qcap2_demuxer_get_program_count(pDmx));

				qcap2_program_info_t* pProgram = qcap2_demuxer_get_program_info(pDmx, 0);
				int nVideoIndex = qcap2_program_info_get_video_source_index(pProgram, 0);
				int nAudioIndex = qcap2_program_info_get_audio_source_index(pProgram, 0);

				LOGD("Prog[%s/%s] V:%d, A:%d", qcap2_program_info_get_metadata(pProgram, "service_name"),
					qcap2_program_info_get_metadata(pProgram, "service_provider"), nVideoIndex, nAudioIndex);

				ULONG nSrcColorSpaceType = QCAP_COLORSPACE_TYPE_UNDEFINED;
				const ULONG nColorSpaceType = QCAP_COLORSPACE_TYPE_NV12;
				const ULONG nVideoEncoderFormat = QCAP_ENCODER_FORMAT_H264;
				const ULONG nVideoBitRate = 10 * 1000000;
				ULONG nVideoWidth = 0;
				ULONG nVideoHeight = 0;
				BOOL bVideoIsInterleaved;
				double dVideoFrameRate;
				ULONG nAudioChannels = 0;
				ULONG nAudioBitsPerSample = 0;
				ULONG nAudioSampleFrequency = 0;
				const ULONG nAudioEncoderFormat = QCAP_ENCODER_FORMAT_AAC_ADTS;

				qcap2_event_t* pVsrcEvent = NULL;
				qcap2_event_t* pAsrcEvent = NULL;

				qcap2_video_encoder_t* pVenc = NULL;
				qcap2_event_t* pVencEvent = NULL;
				qcap2_audio_encoder_t* pAenc = NULL;
				qcap2_event_t* pAencEvent = NULL;
				struct video_format oldVideoFormat;
				struct video_format newVideoFormat;
				struct audio_info oldAudioInfo;
				struct audio_info newAudioInfo;

				memset(&newVideoFormat, 0x00, sizeof(newVideoFormat));
				memset(&newAudioInfo, 0x00, sizeof(newAudioInfo));

				spinlock_lock(current_video_format_spinlock);
				oldVideoFormat = current_video_format;
				spinlock_unlock(current_video_format_spinlock);

				spinlock_lock(current_audio_info_spinlock);
				oldAudioInfo = current_audio_info;
				spinlock_unlock(current_audio_info_spinlock);

				qcap2_video_source_t* pVsrc = qcap2_demuxer_get_video_source(pDmx, nVideoIndex);
				std::shared_ptr<qcap2_video_format_t> pVideoFormat(qcap2_video_format_new(), qcap2_video_format_delete);
				qcap2_video_source_get_video_format(pVsrc, pVideoFormat.get());
				{
					qcap2_video_format_get_property(pVideoFormat.get(), &nSrcColorSpaceType, &nVideoWidth, &nVideoHeight, &bVideoIsInterleaved, &dVideoFrameRate);

					if(nSrcColorSpaceType == QCAP_COLORSPACE_TYPE_UNDEFINED || nVideoWidth <= 0 || nVideoHeight <= 0) {
						LOGI("v: no-link");
					} else {
						LOGI("v: %08X %ux%u'%u, %.2f", nSrcColorSpaceType, nVideoWidth, nVideoHeight, bVideoIsInterleaved, dVideoFrameRate);

						newVideoFormat = (video_format){
							.width = (unsigned int)nVideoWidth,
							.height = (unsigned int)nVideoHeight,
							.framerate = (unsigned int)dVideoFrameRate,
							.colorformat = VIDEO_COLORFORMAT_YUV444,
							.bpp = 8,
							.interlaced = (bVideoIsInterleaved != FALSE),
							.locked = true
						};

						qres = StartVsrc(_FreeStack_, pVsrc, nColorSpaceType,
							nVideoWidth, nVideoHeight, bVideoIsInterleaved, dVideoFrameRate, &pVsrcEvent);
						if(qres != QCAP_RS_SUCCESSFUL) {
							LOGE("%s(%d): StartVsrc() failed, qres=%d", __FUNCTION__, __LINE__, qres);
							break;
						}

#if 1
						qres = StartVenc(_FreeStack_, nColorSpaceType, nVideoWidth, nVideoHeight,
							bVideoIsInterleaved, dVideoFrameRate, nVideoEncoderFormat, nVideoBitRate, &pVenc, &pVencEvent);
						if(qres != QCAP_RS_SUCCESSFUL) {
							LOGE("%s(%d): StartVenc() failed, qres=%d", __FUNCTION__, __LINE__, qres);
							break;
						}
#endif
					}

					spinlock_lock(current_video_format_spinlock);
					current_video_format = newVideoFormat;
					spinlock_unlock(current_video_format_spinlock);

#if BUILD_WITH_DAUSERVICE
					const bool publishCableChange = (! mHavePublishedVideoState) ||
						(oldVideoFormat.locked != newVideoFormat.locked);
					const bool publishVideoChange = (! mHavePublishedVideoState) ||
						(! video_format_equal(oldVideoFormat, newVideoFormat));

					if(pHdmiRxDauServ && publishCableChange) {
						dau_signal_cable_change(pHdmiRxDauServ);
					}

					if(pHdmiRxDauServ && publishVideoChange) {
						dau_signal_video_change(pHdmiRxDauServ);
					}
#endif // BUILD_WITH_DAUSERVICE

					mHavePublishedVideoState = true;
				}

				qcap2_audio_source_t* pAsrc = qcap2_demuxer_get_audio_source(pDmx, nAudioIndex);
				std::shared_ptr<qcap2_audio_format_t> pAudioFormat(qcap2_audio_format_new(), qcap2_audio_format_delete);
				qcap2_audio_source_get_audio_format(pAsrc, pAudioFormat.get());
				{
					qcap2_audio_format_get_property(pAudioFormat.get(), &nAudioChannels, &nAudioBitsPerSample, &nAudioSampleFrequency);

					if(nAudioChannels == 0 || nAudioBitsPerSample == 0 || nAudioSampleFrequency == 0) {
						LOGI("a: no-link");
					} else {
						LOGI("a: %ux%u'%u", nAudioChannels, nAudioBitsPerSample, nAudioSampleFrequency);

						newAudioInfo = (audio_info) {
							.channel_count = (unsigned int)nAudioChannels,
							.speaker_map = AUDIO_SM_FR_FL,
							.level_shift_value = 0,
							.downmix_inhibit = false,
							.lfe_playback_level = 0,
							.sample_rate = (unsigned int)nAudioSampleFrequency,
							.encoding = AUDIO_EN_LPCM,
							.sample_bits = (unsigned int)nAudioBitsPerSample
						};

#if 1
						qres = StartAsrc(_FreeStack_, pAsrc, &pAsrcEvent);
						if(qres != QCAP_RS_SUCCESSFUL) {
							LOGE("%s(%d): StartAsrc() failed, qres=%d", __FUNCTION__, __LINE__, qres);
							break;
						}
#endif

#if 0
						qres = StartAenc(_FreeStack_, nAudioChannels, nAudioBitsPerSample,
							nAudioSampleFrequency, nAudioEncoderFormat, &pAenc, &pAencEvent);
						if(qres != QCAP_RS_SUCCESSFUL) {
							LOGE("%s(%d): StartAenc() failed, qres=%d", __FUNCTION__, __LINE__, qres);
							break;
						}
#endif
					}

					spinlock_lock(current_audio_info_spinlock);
					current_audio_info = newAudioInfo;
					spinlock_unlock(current_audio_info_spinlock);

#if BUILD_WITH_DAUSERVICE
					const bool publishAudioChange = (! mHavePublishedAudioState) ||
						(! audio_info_equal(oldAudioInfo, newAudioInfo));

					if(pHdmiRxDauServ && publishAudioChange) {
						dau_signal_audio_change(pHdmiRxDauServ);
					}
#endif // BUILD_WITH_DAUSERVICE

					mHavePublishedAudioState = true;
				}

				// next level muxers
				qcap2_video_decoder_t* pVdec_rtsp = NULL;
				qcap2_audio_decoder_t* pAdec_rtsp = NULL;
				zst_dante_tx_t* pDanteTx = NULL;
				if(nSrcColorSpaceType != QCAP_COLORSPACE_TYPE_UNDEFINED && nVideoWidth > 0 && nVideoHeight > 0) {
#if 0
					qcap2_muxer_t* pRTSPMuxer;
					qres = StartRTSPMuxer(_FreeStack_, nColorSpaceType, nVideoWidth, nVideoHeight,
						bVideoIsInterleaved, dVideoFrameRate, nVideoEncoderFormat, nVideoBitRate,
						nAudioChannels, nAudioBitsPerSample, nAudioSampleFrequency, nAudioEncoderFormat,
						&pRTSPMuxer, &pVdec_rtsp, &pAdec_rtsp);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): StartRTSPMuxer() failed, qres=%d", __FUNCTION__, __LINE__, qres);
						break;
					}
#endif

#if 1
					qres = StartDanteServer(_FreeStack_, nColorSpaceType, nVideoWidth, nVideoHeight,
						bVideoIsInterleaved, dVideoFrameRate, nVideoEncoderFormat, nVideoBitRate,
						nAudioChannels, nAudioBitsPerSample, nAudioSampleFrequency, nAudioEncoderFormat,
						&pDanteTx);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): StartDanteServer() failed, qres=%d", __FUNCTION__, __LINE__, qres);
						break;
					}
#endif

					if(pVsrcEvent) {
						qres = AddEventHandler(_FreeStack_, pVsrcEvent,
							std::bind(&self_t::OnVsrc, this, pVsrc, pVenc));
						if(qres != QCAP_RS_SUCCESSFUL) {
							LOGE("%s(%d): AddEventHandler() failed, qres=%d", __FUNCTION__, __LINE__, qres);
							break;
						}
					}

					if(pAsrcEvent) {
						qres = AddEventHandler(_FreeStack_, pAsrcEvent,
							std::bind(&self_t::OnAsrc, this, pAsrc, pAenc, pDanteTx));
						if(qres != QCAP_RS_SUCCESSFUL) {
							LOGE("%s(%d): AddEventHandler() failed, qres=%d", __FUNCTION__, __LINE__, qres);
							break;
						}
					}


					if(pVencEvent) {
						qres = AddEventHandler(_FreeStack_, pVencEvent,
							std::bind(&self_t::OnVenc, this, pVenc, pVdec_rtsp, pDanteTx));
						if(qres != QCAP_RS_SUCCESSFUL) {
							LOGE("%s(%d): AddEventHandler() failed, qres=%d", __FUNCTION__, __LINE__, qres);
							break;
						}
					}

					if(pAencEvent) {
						qres = AddEventHandler(_FreeStack_, pAencEvent,
							std::bind(&self_t::OnAenc, this, pAenc, pAdec_rtsp));
						if(qres != QCAP_RS_SUCCESSFUL) {
							LOGE("%s(%d): AddEventHandler() failed, qres=%d", __FUNCTION__, __LINE__, qres);
							break;
						}
					}
				}
			}

			return QCAP_RT_OK;
		}

		QRESULT StartVsrc(free_stack_t& _FreeStack_, qcap2_video_source_t* pVsrc, ULONG nColorSpaceType,
			ULONG nVideoWidth, ULONG nVideoHeight, BOOL bVideoIsInterleaved, double dVideoFrameRate, qcap2_event_t** ppEvent) {
			QRESULT qres = QCAP_RS_SUCCESSFUL;

			switch(1) { case 1:
				qcap2_event_t* pEvent;
				qres = NewEvent(_FreeStack_, &pEvent);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): NewEvent() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				qcap2_video_source_set_frame_count(pVsrc, 4);
				qcap2_video_source_set_event(pVsrc, pEvent);

				{
					std::shared_ptr<qcap2_video_format_t> pVideoFormat(
						qcap2_video_format_new(), qcap2_video_format_delete);

					qcap2_video_format_set_property(pVideoFormat.get(),
						nColorSpaceType, nVideoWidth,
						bVideoIsInterleaved ? nVideoHeight / 2 : nVideoHeight,
						bVideoIsInterleaved, dVideoFrameRate);

					qcap2_video_source_set_video_format(pVsrc, pVideoFormat.get());
				}

				qres = qcap2_video_source_start(pVsrc);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_video_source_start() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
				_FreeStack_ += [pVsrc]() {
					QRESULT qres;

					qres = qcap2_video_source_stop(pVsrc);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): qcap2_video_source_stop() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					}
				};

				*ppEvent = pEvent;
			}

			return qres;
		}

		QRETURN OnVsrc(qcap2_video_source_t* pVsrc, qcap2_video_encoder_t* pVenc) {
			QRESULT qres;

			switch(1) { case 1:
				qcap2_rcbuffer_t* pRCBuffer;
				qres = qcap2_video_source_pop(pVsrc, &pRCBuffer);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_video_source_pop() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
				std::shared_ptr<qcap2_rcbuffer_t> pRCBuffer_(pRCBuffer,
					qcap2_rcbuffer_release);

				if(pVenc) {
					if (++frames_since_idr >= 60) {
						frames_since_idr = 0;
						qcap2_video_encoder_request_idr(pVenc);
						LOGI("requested encoder IDR after 60 input frames");
					}
					qres = qcap2_video_encoder_push(pVenc, pRCBuffer);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): qcap2_video_encoder_push() failed, qres=%d", __FUNCTION__, __LINE__, qres);
						break;
					}
				}

#if 1
				if(nSnapshot > 0) {
					--nSnapshot;

					qres = qcap2_save_raw_video_frame(pRCBuffer, "snapshot");
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): qcap2_save_raw_video_frame() failed, qres=%d", __FUNCTION__, __LINE__, qres);
						break;
					}
				}
#endif
			}

			return QCAP_RT_OK;
		}

		QRESULT StartVenc(free_stack_t& _FreeStack_, ULONG nColorSpaceType, ULONG nVideoWidth, ULONG nVideoHeight,
			BOOL bVideoIsInterleaved, double dVideoFrameRate, ULONG nEncoderFormat, ULONG nVideoBitRate,
			qcap2_video_encoder_t** ppVenc, qcap2_event_t** ppEvent) {
			QRESULT qres = QCAP_RS_SUCCESSFUL;

			switch(1) { case 1:
				qcap2_event_t* pEvent;
				qres = NewEvent(_FreeStack_, &pEvent);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): NewEvent() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				qcap2_video_encoder_t* pVenc = qcap2_video_encoder_new();
				_FreeStack_ += [pVenc]() {
					qcap2_video_encoder_delete(pVenc);
				};

				{
					std::shared_ptr<qcap2_video_encoder_property_t> pVencProp(qcap2_video_encoder_property_new(),
						qcap2_video_encoder_property_delete);

					UINT nGpuNum = 0;
					ULONG nEncoderType = QCAP_ENCODER_TYPE_ALLEGRO2;
					// ULONG nEncoderFormat = QCAP_ENCODER_FORMAT_H265;
					// ULONG nColorSpaceType = nColorSpaceType;
					ULONG nWidth = nVideoWidth;
					ULONG nHeight = nVideoHeight;
					double dFrameRate = dVideoFrameRate;
					ULONG nRecordProfile = QCAP_RECORD_PROFILE_MAIN;
					ULONG nRecordLevel = QCAP_RECORD_LEVEL_52;
					ULONG nRecordEntropy = QCAP_RECORD_ENTROPY_CABAC;
					ULONG nRecordComplexity = QCAP_RECORD_COMPLEXITY_0;
					ULONG nRecordMode = QCAP_RECORD_MODE_CBR;
					ULONG nQuality = 8000;
					ULONG nBitRate = nVideoBitRate;
					ULONG nGOP = dVideoFrameRate > 1.0 ? (ULONG)dVideoFrameRate : (ULONG)1;
					ULONG nBFrames = 0;
					BOOL bIsInterleaved = FALSE;
					ULONG nSlices = 4;
					ULONG nLayers = 0;
					ULONG nSceneCut = 0;
					BOOL bMultiThread = FALSE;
					BOOL bMBBRC = FALSE;
					BOOL bExtBRC = FALSE;
					ULONG nMinQP = 0;
					ULONG nMaxQP = 0;
					ULONG nVBVMaxRate = 0;
					ULONG nVBVBufSize = 0;
					ULONG nAspectRatioX = 0;
					ULONG nAspectRatioY = 0;

					qcap2_video_encoder_property_set_property1(pVencProp.get(), nGpuNum, nEncoderType, nEncoderFormat, nColorSpaceType, nWidth, nHeight, dFrameRate, nRecordProfile, nRecordLevel, nRecordEntropy, nRecordComplexity, nRecordMode, nQuality, nBitRate, nGOP, nBFrames, bIsInterleaved, nSlices, nLayers, nSceneCut, bMultiThread, bMBBRC, bExtBRC, nMinQP, nMaxQP, nVBVMaxRate , nVBVBufSize, nAspectRatioX, nAspectRatioY);
					qcap2_video_encoder_property_set_low_delay(pVencProp.get(), false);
					qcap2_video_encoder_property_set_time_scale_factor(pVencProp.get(), 1);
					qcap2_video_encoder_set_video_property(pVenc, pVencProp.get());
				}

				qcap2_video_encoder_set_multithread(pVenc, false);
				qcap2_video_encoder_set_num_cores(pVenc, 4);
				qcap2_video_encoder_set_filler_ctrl_mode(pVenc, AL_FILLER_APP);
				qcap2_video_encoder_set_event(pVenc, pEvent);

				qres = qcap2_video_encoder_start(pVenc);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGD("%s(%d): qcap2_video_encoder_start() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
				LOGI("H.264 encoder started: %ux%u@%.2f, %u Mbps", (unsigned)nVideoWidth,
					(unsigned)nVideoHeight, dVideoFrameRate, (unsigned)(nVideoBitRate / 1000000));
				_FreeStack_ += [pVenc]() {
					QRESULT qres;

					qres = qcap2_video_encoder_stop(pVenc);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGD("%s(%d): qcap2_video_encoder_stop() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					}
				};

				*ppVenc = pVenc;
				*ppEvent = pEvent;
			}

			return qres;
		}

		QRETURN OnVenc(qcap2_video_encoder_t* pVenc, qcap2_video_decoder_t* pVdec_rtsp, zst_dante_tx_t* pDanteTx) {
			QRESULT qres;

			switch(1) { case 1:
				qcap2_rcbuffer_t* pRCBuffer;
				qres = qcap2_video_encoder_pop(pVenc, &pRCBuffer);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_video_encoder_pop() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
				std::shared_ptr<qcap2_rcbuffer_t> pRCBuffer_(pRCBuffer,
					qcap2_rcbuffer_release);

				if(pVdec_rtsp) {
					qres = qcap2_video_decoder_push(pVdec_rtsp, pRCBuffer);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): qcap2_video_decoder_push() failed, qres=%d", __FUNCTION__, __LINE__, qres);
						break;
					}
				}

				if(pDanteTx && pDanteTx->app_video_src_pad) {
					std::shared_ptr<qcap2_av_packet_t> pAVPacket((qcap2_av_packet_t*)qcap2_rcbuffer_lock_data(pRCBuffer),
						[pRCBuffer](qcap2_av_packet_t*) {
							qcap2_rcbuffer_unlock_data(pRCBuffer);
						}
					);

					uint8_t* pStreamBuffer = nullptr;
					int nStreamBufferLen = 0;
					qcap2_av_packet_get_buffer(pAVPacket.get(), &pStreamBuffer, &nStreamBufferLen);

					int nStreamIndex = 0;
					BOOL bIsKeyFrame = FALSE;
					qcap2_av_packet_get_property(pAVPacket.get(), &nStreamIndex, &bIsKeyFrame);

					double dSampleTime = 0.0;
					qcap2_av_packet_get_sample_time(pAVPacket.get(), &dSampleTime);

					if (pStreamBuffer && nStreamBufferLen > 0) {
						std::vector<uint8_t> parameter_sets;
						bool has_idr = false;
						bool has_sps = false;
						bool has_pps = false;
						for (size_t pos = 0; pos + 4 <= (size_t)nStreamBufferLen;) {
							size_t prefix = pStreamBuffer[pos] == 0 && pStreamBuffer[pos + 1] == 0 &&
								pStreamBuffer[pos + 2] == 1 ? 3 :
								(pos + 4 <= (size_t)nStreamBufferLen && pStreamBuffer[pos] == 0 &&
								 pStreamBuffer[pos + 1] == 0 && pStreamBuffer[pos + 2] == 0 &&
								 pStreamBuffer[pos + 3] == 1 ? 4 : 0);
							if (!prefix) { ++pos; continue; }
							size_t nal = pos + prefix;
							size_t end = nal + 1;
							while (end + 3 < (size_t)nStreamBufferLen &&
								!(pStreamBuffer[end] == 0 && pStreamBuffer[end + 1] == 0 &&
								  (pStreamBuffer[end + 2] == 1 || (end + 3 < (size_t)nStreamBufferLen &&
								   pStreamBuffer[end + 2] == 0 && pStreamBuffer[end + 3] == 1)))) ++end;
							uint8_t type = pStreamBuffer[nal] & 0x1f;
							if (type == 7 || type == 8) {
								if (type == 7) has_sps = true;
								else has_pps = true;
								parameter_sets.insert(parameter_sets.end(), {0, 0, 0, 1});
								parameter_sets.insert(parameter_sets.end(), pStreamBuffer + nal, pStreamBuffer + end);
							} else if (type == 5) {
								has_idr = true;
							}
							pos = end;
						}
						if (has_sps && has_pps)
							pDanteTx->h264_parameter_sets = parameter_sets;

						std::vector<uint8_t> keyframe_with_config;
						if ((bIsKeyFrame || has_idr) && !(has_sps && has_pps) &&
							!pDanteTx->h264_parameter_sets.empty()) {
							keyframe_with_config = pDanteTx->h264_parameter_sets;
							keyframe_with_config.insert(keyframe_with_config.end(), pStreamBuffer,
								pStreamBuffer + nStreamBufferLen);
							pStreamBuffer = keyframe_with_config.data();
							nStreamBufferLen = (int)keyframe_with_config.size();
							LOGI("prepended cached SPS/PPS to H.264 keyframe");
						}
						if (has_idr)
							LOGI("encoded H.264 IDR received (keyframe=%d, SPS/PPS=%s)",
								(int)bIsKeyFrame, has_sps && has_pps ? "inline" : "cached");
						if (g_tx_dump) {
							if (fwrite(pStreamBuffer, 1, (size_t)nStreamBufferLen, g_tx_dump) != (size_t)nStreamBufferLen ||
							    fflush(g_tx_dump) != 0) {
								fclose(g_tx_dump);
								g_tx_dump = NULL;
							}
						}
						zst_buffer_t* zbuf = zst_buffer_create(ZST_BUFFER_VIDEO_PACKET);
						if (zbuf) {
							void* copy_data = malloc((size_t)nStreamBufferLen);
							if (copy_data) {
								memcpy(copy_data, pStreamBuffer, (size_t)nStreamBufferLen);
								zbuf->memory.type = ZST_MEMORY_CPU;
								zbuf->memory.data = copy_data;
								zbuf->memory.size = (size_t)nStreamBufferLen;
								zbuf->memory.priv = copy_data;
								zbuf->memory.release = free;
								zbuf->pts = (zst_time_t)(dSampleTime * 1000000000.0);
								zbuf->dts = zbuf->pts;
								if (bIsKeyFrame || has_idr) {
									zbuf->flags |= ZST_BUFFER_FLAG_KEYFRAME;
								}
								zst_pad_push(pDanteTx->app_video_src_pad, zbuf);
								if (pDanteTx->app_video_mirror_src_pad)
									zst_pad_push(pDanteTx->app_video_mirror_src_pad, zbuf);
								pDanteTx->tx_video_frames++;
								pDanteTx->tx_video_bytes += (size_t)nStreamBufferLen;
							}
							zst_buffer_unref(zbuf);
						}
					}
				}
			}

			return QCAP_RT_OK;
		}

		QRESULT StartAsrc(free_stack_t& _FreeStack_, qcap2_audio_source_t* pAsrc, qcap2_event_t** ppEvent) {
			QRESULT qres = QCAP_RS_SUCCESSFUL;

			switch(1) { case 1:
				qcap2_event_t* pEvent;
				qres = NewEvent(_FreeStack_, &pEvent);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): NewEvent() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				qcap2_audio_source_set_event(pAsrc, pEvent);

				qres = qcap2_audio_source_start(pAsrc);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_audio_source_start() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
				_FreeStack_ += [pAsrc]() {
					QRESULT qres;

					qres = qcap2_audio_source_stop(pAsrc);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): qcap2_audio_source_stop() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					}
				};

				*ppEvent = pEvent;
			}

			return qres;
		}

		static void audio_wrap_buf_destroy(zst_buffer_t* b) {
			if (b && b->payload) {
				free(b->payload);
				b->payload = NULL;
			}
		}

		QRETURN OnAsrc(qcap2_audio_source_t* pAsrc, qcap2_audio_encoder_t* pAenc, zst_dante_tx_t* pDanteTx) {
			QRESULT qres;

			switch(1) { case 1:
				qcap2_rcbuffer_t* pRCBuffer;
				qres = qcap2_audio_source_pop(pAsrc, &pRCBuffer);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_audio_source_pop() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
				std::shared_ptr<qcap2_rcbuffer_t> pRCBuffer_(pRCBuffer,
					qcap2_rcbuffer_release);

				if(pAenc) {
					qres = qcap2_audio_encoder_push(pAenc, pRCBuffer);
					if(qres != QCAP_RS_SUCCESSFUL) {
						// LOGE("%s(%d): qcap2_audio_encoder_push() failed, qres=%d", __FUNCTION__, __LINE__, qres);
						break;
					}
				}

				if(pDanteTx && pDanteTx->app_audio_src_pad) {
					std::shared_ptr<qcap2_av_frame_t> pAVFrame((qcap2_av_frame_t*)qcap2_rcbuffer_lock_data(pRCBuffer),
						[pRCBuffer](qcap2_av_frame_t*) {
							qcap2_rcbuffer_unlock_data(pRCBuffer);
						}
					);

					uint8_t* pFrameBuffer = nullptr;
					int nFrameBufferStride = 0;
					qcap2_av_frame_get_buffer(pAVFrame.get(), &pFrameBuffer, &nFrameBufferStride);

					ULONG nChannels = 0, nSampleFmt = 0, nSampleFrequency = 0, nFrameSamples = 0;
					qcap2_av_frame_get_audio_property(pAVFrame.get(), &nChannels, &nSampleFmt, &nSampleFrequency, &nFrameSamples);

					double dSampleTime = 0.0;
					qcap2_av_frame_get_sample_time(pAVFrame.get(), &dSampleTime);

					uint32_t ch = nChannels ? (uint32_t)nChannels : (pDanteTx->audio_channels ? pDanteTx->audio_channels : 2);
					uint32_t sr = nSampleFrequency ? (uint32_t)nSampleFrequency : (pDanteTx->audio_sample_rate ? pDanteTx->audio_sample_rate : 48000);
					uint32_t sample_bytes = (pDanteTx->audio_bits > 0 ? pDanteTx->audio_bits / 8 : 2);
					size_t nFrameBufferLen = (size_t)nFrameBufferStride;
					if (nFrameBufferLen == 0 && nFrameSamples > 0) {
						nFrameBufferLen = (size_t)nFrameSamples * ch * sample_bytes;
					}

					if (pFrameBuffer && nFrameBufferLen > 0) {
						zst_buffer_t* zbuf = zst_buffer_create(ZST_BUFFER_AUDIO_FRAME);
						if (zbuf) {
							void* copy_data = malloc(nFrameBufferLen);
							if (copy_data) {
								memcpy(copy_data, pFrameBuffer, nFrameBufferLen);
								zbuf->memory.type = ZST_MEMORY_CPU;
								zbuf->memory.data = copy_data;
								zbuf->memory.size = nFrameBufferLen;
								zbuf->memory.priv = copy_data;
								zbuf->memory.release = free;
								zbuf->pts = (zst_time_t)(dSampleTime * 1000000000.0);
								zbuf->dts = zbuf->pts;
								zbuf->destroy = audio_wrap_buf_destroy;

								zst_audio_frame_t* af = (zst_audio_frame_t*)calloc(1, sizeof(zst_audio_frame_t));
								af->sample_rate = sr;
								af->channels = ch;
								af->format = (pDanteTx->audio_bits == 32) ? ZST_AUDIO_FMT_S32LE : ZST_AUDIO_FMT_S16LE;
								uint32_t bytes_per_sample = ch * sample_bytes;
								af->nb_samples = bytes_per_sample > 0 ? (uint32_t)(nFrameBufferLen / bytes_per_sample) : (uint32_t)nFrameSamples;
								af->data = copy_data;
								zbuf->payload = af;

								zst_pad_push(pDanteTx->app_audio_src_pad, zbuf);
								pDanteTx->tx_audio_frames++;
								pDanteTx->tx_audio_bytes += nFrameBufferLen;
								pDanteTx->tx_audio_samples += af->nb_samples;
							}
							zst_buffer_unref(zbuf);
						}
					}
				}
			}

			return QCAP_RT_OK;
		}

		QRESULT StartAenc(free_stack_t& _FreeStack_, ULONG nAudioChannels, ULONG nAudioBitsPerSample,
				ULONG nAudioSampleFrequency, ULONG nAudioEncoderFormat, qcap2_audio_encoder_t** ppAenc, qcap2_event_t** ppEvent) {
			QRESULT qres = QCAP_RS_SUCCESSFUL;

			switch(1) { case 1:
				qcap2_event_t* pEvent;
				qres = NewEvent(_FreeStack_, &pEvent);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): NewEvent() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				qcap2_audio_encoder_t* pAenc = qcap2_audio_encoder_new();
				_FreeStack_ += [pAenc]() {
					qcap2_audio_encoder_delete(pAenc);
				};

				{
					std::shared_ptr<qcap2_audio_encoder_property_t> pAencProp(qcap2_audio_encoder_property_new(),
						qcap2_audio_encoder_property_delete);

					qcap2_audio_encoder_property_set_property(pAencProp.get(),
						QCAP_ENCODER_TYPE_SOFTWARE, nAudioEncoderFormat,
						nAudioChannels, nAudioBitsPerSample, nAudioSampleFrequency);
					qcap2_audio_encoder_set_audio_property(pAenc, pAencProp.get());
				}

				qcap2_audio_encoder_set_event(pAenc, pEvent);
				qcap2_audio_encoder_set_frame_count(pAenc, 16);
				qcap2_audio_encoder_set_packet_count(pAenc, 16);
				qcap2_audio_encoder_set_multithread(pAenc, true);

				qres = qcap2_audio_encoder_start(pAenc);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGD("%s(%d): qcap2_audio_encoder_start() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
				_FreeStack_ += [pAenc]() {
					QRESULT qres;

					qres = qcap2_audio_encoder_stop(pAenc);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGD("%s(%d): qcap2_audio_encoder_stop() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					}
				};

				*ppAenc = pAenc;
				*ppEvent = pEvent;
			}

			return qres;
		}

		QRETURN OnAenc(qcap2_audio_encoder_t* pAenc, qcap2_audio_decoder_t* pAdec_rtsp) {
			QRESULT qres;

			switch(1) { case 1:
				qcap2_rcbuffer_t* pRCBuffer;
				qres = qcap2_audio_encoder_pop(pAenc, &pRCBuffer);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_audio_encoder_pop() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
				std::shared_ptr<qcap2_rcbuffer_t> pRCBuffer_(pRCBuffer,
					qcap2_rcbuffer_release);

				if(pAdec_rtsp) {
					qres = qcap2_audio_decoder_push(pAdec_rtsp, pRCBuffer);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): qcap2_audio_decoder_push() failed, qres=%d", __FUNCTION__, __LINE__, qres);
						break;
					}
				}
			}

			return QCAP_RT_OK;
		}

		QRESULT StartRTSPMuxer(free_stack_t& _FreeStack_, ULONG nColorSpaceType, ULONG nVideoWidth, ULONG nVideoHeight,
			BOOL bVideoIsInterleaved, double dVideoFrameRate, ULONG nEncoderFormat, ULONG nVideoBitRate,
			ULONG nAudioChannels, ULONG nAudioBitsPerSample, ULONG nAudioSampleFrequency, ULONG nAudioEncoderFormat,
			qcap2_muxer_t** ppMuxer, qcap2_video_decoder_t** ppVdec, qcap2_audio_decoder_t** ppAdec) {
			QRESULT qres = QCAP_RS_SUCCESSFUL;

			switch(1) { case 1:
				qcap2_muxer_t* pMuxer = qcap2_muxer_new();
				_FreeStack_ += [pMuxer]() {
					qcap2_muxer_delete(pMuxer);
				};

				qcap2_muxer_set_type(pMuxer, QCAP2_MUXER_TYPE_RTSP);
				qcap2_muxer_set_endpoint(pMuxer, "0.0.0.0", 554);
				qcap2_muxer_set_max_threads(pMuxer, 4);
				qcap2_muxer_set_realm(pMuxer, "YUAN");
				qcap2_muxer_add_user(pMuxer, "root", "root");
				qcap2_muxer_add_user(pMuxer, "guest", "guest");

				{
					std::shared_ptr<qcap2_program_info_t> pProgInfo(
						qcap2_program_info_new(), qcap2_program_info_delete);

					qcap2_program_info_set_metadata(pProgInfo.get(), "cname", "YUAN");
					qcap2_program_info_set_metadata(pProgInfo.get(), "resource_name", "session0");
					qcap2_program_info_set_video_decoder_count(pProgInfo.get(), 1);
					qcap2_program_info_set_video_decoder_index(pProgInfo.get(), 0, 0);
					qcap2_program_info_set_audio_decoder_count(pProgInfo.get(), 1);
					qcap2_program_info_set_audio_decoder_index(pProgInfo.get(), 0, 0);
					qcap2_muxer_add_program_info(pMuxer, pProgInfo.get());
				}

				qres = qcap2_muxer_start(pMuxer);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_muxer_start() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
				_FreeStack_ += [pMuxer]() {
					QRESULT qres;

					qres = qcap2_muxer_stop(pMuxer);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): qcap2_muxer_stop() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					}
				};

				qcap2_video_decoder_t* pVdec = qcap2_muxer_get_video_decoder(pMuxer, 0);
				{
					std::shared_ptr<qcap2_video_encoder_property_t> pVencProp(
						qcap2_video_encoder_property_new(), qcap2_video_encoder_property_delete);

					UINT nGpuNum = 0;
					ULONG nEncoderType = QCAP_ENCODER_TYPE_SOFTWARE;
					// ULONG nEncoderFormat = QCAP_ENCODER_FORMAT_H264;
					// ULONG nColorSpaceType = XX;
					ULONG nWidth = nVideoWidth;
					ULONG nHeight = nVideoHeight;
					double dFrameRate = dVideoFrameRate;
					ULONG nRecordProfile = QCAP_RECORD_PROFILE_HIGH;
					ULONG nRecordLevel = QCAP_RECORD_LEVEL_51;
					ULONG nRecordEntropy = QCAP_RECORD_ENTROPY_CABAC;
					ULONG nRecordComplexity = QCAP_RECORD_COMPLEXITY_0;
					ULONG nRecordMode = QCAP_RECORD_MODE_CBR;
					ULONG nQuality = 8000;
					ULONG nBitRate = nVideoBitRate;
					ULONG nGOP = dVideoFrameRate > 1.0 ? (ULONG)dVideoFrameRate : (ULONG)1;
					ULONG nBFrames = 0;
					BOOL bIsInterleaved = FALSE;
					ULONG nSlices = 0;
					ULONG nLayers = 0;
					ULONG nSceneCut = 0;
					BOOL bMultiThread = FALSE;
					BOOL bMBBRC = FALSE;
					BOOL bExtBRC = FALSE;
					ULONG nMinQP = 0;
					ULONG nMaxQP = 0;
					ULONG nVBVMaxRate = 0;
					ULONG nVBVBufSize = 0;
					ULONG nAspectRatioX = 0;
					ULONG nAspectRatioY = 0;

					qcap2_video_encoder_property_set_property1(pVencProp.get(), nGpuNum, nEncoderType, nEncoderFormat, nColorSpaceType, nWidth, nHeight, dFrameRate, nRecordProfile, nRecordLevel, nRecordEntropy, nRecordComplexity, nRecordMode, nQuality, nBitRate, nGOP, nBFrames, bIsInterleaved, nSlices, nLayers, nSceneCut, bMultiThread, bMBBRC, bExtBRC, nMinQP, nMaxQP, nVBVMaxRate , nVBVBufSize, nAspectRatioX, nAspectRatioY);
					qcap2_video_decoder_set_video_property(pVdec, pVencProp.get());
				}
				// qcap2_video_decoder_set_payload_type(pVdec, 98);

				qcap2_audio_decoder_t* pAdec = qcap2_muxer_get_audio_decoder(pMuxer, 0);
				{
					std::shared_ptr<qcap2_audio_encoder_property_t> pAencProp(qcap2_audio_encoder_property_new(),
						qcap2_audio_encoder_property_delete);

					qcap2_audio_encoder_property_set_property(pAencProp.get(),
						QCAP_ENCODER_TYPE_SOFTWARE, nAudioEncoderFormat,
						nAudioChannels, nAudioBitsPerSample, nAudioSampleFrequency);
					qcap2_audio_decoder_set_audio_property(pAdec, pAencProp.get());
				}
				// qcap2_audio_decoder_set_payload_type(pAdec, 96);

				qres = qcap2_muxer_play(pMuxer);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_muxer_play() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				qres = qcap2_video_decoder_start(pVdec);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_video_decoder_start() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
				_FreeStack_ += [pVdec]() {
					QRESULT qres;

					qres = qcap2_video_decoder_stop(pVdec);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): qcap2_video_decoder_stop() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					}
				};

				qres = qcap2_audio_decoder_start(pAdec);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_audio_decoder_start() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
				_FreeStack_ += [pAdec]() {
					QRESULT qres;

					qres = qcap2_audio_decoder_stop(pAdec);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): qcap2_audio_decoder_stop() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					}
				};

				*ppMuxer = pMuxer;
				*ppVdec = pVdec;
				*ppAdec = pAdec;
			}

			return qres;
		}

		static void set_str(zst_element_t* el, const char* p, const char* v) {
			zst_element_set_property(el, p, v);
		}
		static void set_uint(zst_element_t* el, const char* p, uint64_t v) {
			char buf[32]; snprintf(buf, sizeof(buf), "%lu", (unsigned long)v);
			zst_element_set_property(el, p, buf);
		}
		static uint64_t get_uint_prop(zst_element_t* el, const char* p) {
			char buf[32] = {0};
			if (zst_element_get_property(el, p, buf, sizeof(buf)) == ZST_OK)
				return (uint64_t)strtoull(buf, NULL, 10);
			return 0;
		}

		static void* dante_tx_bus_worker(void* arg) {
			zst_dante_tx_t* tx = (zst_dante_tx_t*)arg;
			uint64_t last_stat_time = 0;
			uint64_t last_video_frames = 0;
			uint64_t last_video_bytes = 0;
			uint64_t last_audio_frames = 0;
			uint64_t last_audio_bytes = 0;
			uint64_t last_audio_samples = 0;

			while (tx->running) {
				zst_event_t* ev = NULL;
				if (zst_bus_pop(tx->pipe->bus, &ev, 100) == ZST_OK && ev) {
					if (ev->type == ZST_EVENT_DANTE_FLOW_CREATED) {
						LOGI("[DANTE BUS] TX video flow created: idx=%u ch=%u port=%u to=%s",
							ev->as.dante_flow.flow.flow_index,
							ev->as.dante_flow.flow.channel_index,
							ev->as.dante_flow.flow.port,
							ev->as.dante_flow.flow.receiver_address ? ev->as.dante_flow.flow.receiver_address : "?");
						zst_dante_video_coordinator_apply_flow(tx->coordinator, &ev->as.dante_flow.flow);
					} else if (ev->type == ZST_EVENT_DANTE_FLOW_DELETED) {
						LOGI("[DANTE BUS] TX video flow deleted: idx=%u", ev->as.dante_flow.flow.flow_index);
						zst_dante_video_coordinator_remove_flow(tx->coordinator, &ev->as.dante_flow.flow);
					} else if (ev->type == ZST_EVENT_ERROR) {
						LOGE("[DANTE BUS] Pipeline error: %s", ev->as.error.message ? ev->as.error.message : "");
					}
					zst_event_destroy(ev);
				}

				struct timespec ts;
				clock_gettime(CLOCK_MONOTONIC, &ts);
				uint64_t now_ms = (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
				uint64_t video_frames = tx->tx_video_frames.load();
				uint64_t video_bytes = tx->tx_video_bytes.load();
				uint64_t audio_frames = tx->tx_audio_frames.load();
				uint64_t audio_bytes = tx->tx_audio_bytes.load();
				uint64_t audio_samples = tx->tx_audio_samples.load();
				if (last_stat_time == 0) {
					last_stat_time = now_ms;
					last_video_frames = video_frames;
					last_video_bytes = video_bytes;
					last_audio_frames = audio_frames;
					last_audio_bytes = audio_bytes;
					last_audio_samples = audio_samples;
					continue;
				}
				if (now_ms - last_stat_time >= 3000) {
					double elapsed_sec = (double)(now_ms - last_stat_time) / 1000.0;
					uint32_t active_flows = zst_dante_video_coordinator_get_flow_count(tx->coordinator);
					double video_fps = elapsed_sec > 0.0
						? (double)(video_frames - last_video_frames) / elapsed_sec : 0.0;
					double video_mbps = elapsed_sec > 0.0
						? (double)(video_bytes - last_video_bytes) * 8.0 / (elapsed_sec * 1000000.0) : 0.0;
					double audio_frame_rate = elapsed_sec > 0.0
						? (double)(audio_frames - last_audio_frames) / elapsed_sec : 0.0;
					double audio_sample_rate = elapsed_sec > 0.0
						? (double)(audio_samples - last_audio_samples) / elapsed_sec : 0.0;
					double audio_mbps = elapsed_sec > 0.0
						? (double)(audio_bytes - last_audio_bytes) * 8.0 / (elapsed_sec * 1000000.0) : 0.0;

					LOGI("[DANTE TX STATS] flows=%u | VIDEO: %lu frames (%.1f MB, %.1f fps, %.2f Mbps) | AUDIO: %lu frames (%.1f MB, %.1f frames/s, %.0f samples/s, %.2f Mbps)",
						active_flows,
						(unsigned long)video_frames,
						(double)video_bytes / (1024.0 * 1024.0), video_fps, video_mbps,
						(unsigned long)audio_frames,
						(double)audio_bytes / (1024.0 * 1024.0), audio_frame_rate,
						audio_sample_rate, audio_mbps);

					for (uint32_t fi = 0; fi < active_flows; fi++) {
							zst_element_t* rts = zst_dante_video_coordinator_get_tx_udp_sink(tx->coordinator, fi);
							if (rts) {
								uint64_t pkt = get_uint_prop(rts, ZST_DANTE_UDP_SINK_PROP_PACKETS_SENT);
								uint64_t byt = get_uint_prop(rts, ZST_DANTE_UDP_SINK_PROP_BYTES_SENT);
								uint64_t err = get_uint_prop(rts, ZST_DANTE_UDP_SINK_PROP_SEND_ERRORS);
								uint64_t eagain = get_uint_prop(rts, ZST_DANTE_UDP_SINK_PROP_SEND_ERRORS_EAGAIN);
								uint64_t eintr = get_uint_prop(rts, ZST_DANTE_UDP_SINK_PROP_SEND_ERRORS_EINTR);
								uint64_t othr = get_uint_prop(rts, ZST_DANTE_UDP_SINK_PROP_SEND_ERRORS_OTHER);
								uint64_t lerr = get_uint_prop(rts, ZST_DANTE_UDP_SINK_PROP_SEND_LAST_ERRNO);
								uint64_t sndbuf = get_uint_prop(rts, ZST_DANTE_UDP_SINK_PROP_SEND_BUFFER_SIZE);
								uint64_t gap_count = get_uint_prop(rts, ZST_DANTE_UDP_SINK_PROP_SEND_GAP_COUNT);
								uint64_t gap_total = get_uint_prop(rts, ZST_DANTE_UDP_SINK_PROP_SEND_GAP_TOTAL_NS);
								uint64_t gap_min = get_uint_prop(rts, ZST_DANTE_UDP_SINK_PROP_SEND_GAP_MIN_NS);
								uint64_t gap_max = get_uint_prop(rts, ZST_DANTE_UDP_SINK_PROP_SEND_GAP_MAX_NS);
								uint64_t gap_5us = get_uint_prop(rts, ZST_DANTE_UDP_SINK_PROP_SEND_GAP_LE_5US);
								uint64_t gap_20us = get_uint_prop(rts, ZST_DANTE_UDP_SINK_PROP_SEND_GAP_LE_20US);
								uint64_t gap_100us = get_uint_prop(rts, ZST_DANTE_UDP_SINK_PROP_SEND_GAP_LE_100US);
								uint64_t send_total = get_uint_prop(rts, ZST_DANTE_UDP_SINK_PROP_SEND_DURATION_TOTAL_NS);
								uint64_t send_min = get_uint_prop(rts, ZST_DANTE_UDP_SINK_PROP_SEND_DURATION_MIN_NS);
								uint64_t send_max = get_uint_prop(rts, ZST_DANTE_UDP_SINK_PROP_SEND_DURATION_MAX_NS);
								uint64_t eagain_gap = get_uint_prop(rts, ZST_DANTE_UDP_SINK_PROP_EAGAIN_LAST_GAP_NS);
								uint64_t eagain_burst = get_uint_prop(rts, ZST_DANTE_UDP_SINK_PROP_EAGAIN_LAST_BURST_PACKETS);
								uint64_t eagain_max_burst = get_uint_prop(rts, ZST_DANTE_UDP_SINK_PROP_EAGAIN_MAX_BURST_PACKETS);
								LOGI("[DANTE TX ROUTE idx=%u] sent %llu pkts (%.1f MB), %llu send-errors "
									"(eagain=%llu eintr=%llu other=%llu last_errno=%llu sndbuf=%llu)",
									fi, (unsigned long long)pkt,
									(double)byt / (1024.0 * 1024.0), (unsigned long long)err,
									(unsigned long long)eagain, (unsigned long long)eintr,
									(unsigned long long)othr, (unsigned long long)lerr,
									(unsigned long long)sndbuf);
								if (gap_count) {
									LOGI("[DANTE TX TIMING idx=%u] gaps=%llu avg=%.1fns min=%lluns max=%lluns "
										"le5us=%llu le20us=%llu le100us=%llu | sendto avg=%.1fns min=%lluns max=%lluns "
										"eagain-last-gap=%lluns burst=%llu max-burst=%llu",
										fi, (unsigned long long)gap_count, (double)gap_total / gap_count,
										(unsigned long long)gap_min, (unsigned long long)gap_max,
										(unsigned long long)gap_5us, (unsigned long long)gap_20us,
										(unsigned long long)gap_100us, (double)send_total / (gap_count + 1),
										(unsigned long long)send_min, (unsigned long long)send_max,
										(unsigned long long)eagain_gap, (unsigned long long)eagain_burst,
										(unsigned long long)eagain_max_burst);
								}
								zst_element_t* probe = zst_dante_video_coordinator_get_tx_probe_udp_sink(
									tx->coordinator, fi);
								if (probe) {
									uint64_t probe_pkt = get_uint_prop(probe, ZST_DANTE_UDP_SINK_PROP_PACKETS_SENT);
									uint64_t probe_err = get_uint_prop(probe, ZST_DANTE_UDP_SINK_PROP_SEND_ERRORS);
									uint64_t probe_eagain = get_uint_prop(probe, ZST_DANTE_UDP_SINK_PROP_SEND_ERRORS_EAGAIN);
									LOGI("[DANTE TX PROBE idx=%u port+10] sent %llu pkts, %llu send-errors "
										"(eagain=%llu)",
										fi, (unsigned long long)probe_pkt, (unsigned long long)probe_err,
										(unsigned long long)probe_eagain);
								}
							}
						}
						if (tx->mirror_udp_sink) {
							uint64_t pkt = get_uint_prop(tx->mirror_udp_sink, ZST_DANTE_UDP_SINK_PROP_PACKETS_SENT);
							uint64_t byt = get_uint_prop(tx->mirror_udp_sink, ZST_DANTE_UDP_SINK_PROP_BYTES_SENT);
							uint64_t err = get_uint_prop(tx->mirror_udp_sink, ZST_DANTE_UDP_SINK_PROP_SEND_ERRORS);
							uint64_t eagain = get_uint_prop(tx->mirror_udp_sink, ZST_DANTE_UDP_SINK_PROP_SEND_ERRORS_EAGAIN);
							uint64_t eintr = get_uint_prop(tx->mirror_udp_sink, ZST_DANTE_UDP_SINK_PROP_SEND_ERRORS_EINTR);
							uint64_t othr = get_uint_prop(tx->mirror_udp_sink, ZST_DANTE_UDP_SINK_PROP_SEND_ERRORS_OTHER);
							uint64_t lerr = get_uint_prop(tx->mirror_udp_sink, ZST_DANTE_UDP_SINK_PROP_SEND_LAST_ERRNO);
							uint64_t sndbuf = get_uint_prop(tx->mirror_udp_sink, ZST_DANTE_UDP_SINK_PROP_SEND_BUFFER_SIZE);
							LOGI("[MIRROR TX ROUTE] sent %llu pkts (%.1f MB), %llu send-errors "
								"(eagain=%llu eintr=%llu other=%llu last_errno=%llu sndbuf=%llu)",
								(unsigned long long)pkt,
								(double)byt / (1024.0 * 1024.0), (unsigned long long)err,
								(unsigned long long)eagain, (unsigned long long)eintr,
								(unsigned long long)othr, (unsigned long long)lerr,
								(unsigned long long)sndbuf);
						}

					last_stat_time = now_ms;
					last_video_frames = video_frames;
					last_video_bytes = video_bytes;
					last_audio_frames = audio_frames;
					last_audio_bytes = audio_bytes;
					last_audio_samples = audio_samples;
				}
			}
			return NULL;
		}

		QRESULT StartDanteServer(free_stack_t& _FreeStack_, ULONG nColorSpaceType, ULONG nVideoWidth, ULONG nVideoHeight,
			BOOL bVideoIsInterleaved, double dVideoFrameRate, ULONG nEncoderFormat, ULONG nVideoBitRate,
			ULONG nAudioChannels, ULONG nAudioBitsPerSample, ULONG nAudioSampleFrequency, ULONG nAudioEncoderFormat,
			zst_dante_tx_t** ppDanteTx)
		{
			QRESULT qres = QCAP_RS_SUCCESSFUL;

			LOGI("StartDanteServer (zstreamer): V: %ux%u@%.2f (bitrate=%u) A: %uch %ubit %uHz",
				(unsigned)nVideoWidth, (unsigned)nVideoHeight, dVideoFrameRate, (unsigned)nVideoBitRate,
				(unsigned)nAudioChannels, (unsigned)nAudioBitsPerSample, (unsigned)nAudioSampleFrequency);

			zst_dante_tx_t* tx = new zst_dante_tx_t();
			tx->audio_channels = nAudioChannels ? nAudioChannels : 2;
			tx->audio_sample_rate = nAudioSampleFrequency ? nAudioSampleFrequency : 48000;
			tx->audio_bits = nAudioBitsPerSample ? nAudioBitsPerSample : 16;
			tx->video_width = nVideoWidth;
			tx->video_height = nVideoHeight;
			tx->video_fps = dVideoFrameRate > 0 ? dVideoFrameRate : 30.0;
			tx->direct_mode = !g_tx_direct_rx_ip.empty();
			tx->direct_rx_ip = g_tx_direct_rx_ip;
			tx->direct_port = g_tx_direct_port;
			const bool direct_mode = tx->direct_mode;
			struct timespec start_ts;
			clock_gettime(CLOCK_MONOTONIC, &start_ts);
			tx->start_time_ms = (uint64_t)start_ts.tv_sec * 1000 + start_ts.tv_nsec / 1000000;

			tx->pipe = zst_pipeline_create();
			if (!tx->pipe) {
				LOGE("zst_pipeline_create failed");
				delete tx;
				return (QRESULT)-1;
			}

			/* 1. Dante DVR Session (skipped in direct RTP mode) */
			if (!direct_mode) {
				tx->session = zst_dante_session_create("/var/run/dante/dvr");
				set_uint(tx->session, "tx-video-channels", 1);
				set_uint(tx->session, "rx-video-channels", 1);
			}

			/* 2. Dante Video Coordinator */
			tx->coordinator = zst_dante_video_coordinator_create();
			if (tx->session)
				zst_dante_video_coordinator_attach_session(tx->coordinator, tx->session);
			tx->tx_video_pad = zst_dante_video_coordinator_request_tx_input_pad(tx->coordinator, 0);

			tx->app_video_src_pad = zst_pad_create("app_video_src", ZST_PAD_SRC);
			zst_pad_link(tx->app_video_src_pad, tx->tx_video_pad);
			if (!direct_mode && !g_tx_direct_mirror_rx_ip.empty()) {
				tx->mirror_rtp_payloader = zst_rtp_payloader_create();
				tx->mirror_udp_sink = zst_dante_udp_sink_create();
				if (!tx->mirror_rtp_payloader || !tx->mirror_udp_sink) {
					LOGE("direct mirror RTP elements creation failed");
					return (QRESULT)-1;
				}
				set_str(tx->mirror_rtp_payloader, "codec", "h264");
				set_uint(tx->mirror_rtp_payloader, "payload-type", 96);
				set_uint(tx->mirror_rtp_payloader, "mtu", 1200);
				set_str(tx->mirror_udp_sink, "destination-address", g_tx_direct_mirror_rx_ip.c_str());
				set_uint(tx->mirror_udp_sink, "port", g_tx_direct_mirror_port);
				tx->app_video_mirror_src_pad = zst_pad_create("app_video_mirror_src", ZST_PAD_SRC);
				zst_pad_link(tx->app_video_mirror_src_pad,
					zst_element_get_pad(tx->mirror_rtp_payloader, "sink"));
				zst_pad_link(zst_element_get_pad(tx->mirror_rtp_payloader, "src"),
					zst_element_get_pad(tx->mirror_udp_sink, "sink"));
				LOGI("[DIRECT MIRROR TX] standalone RTP to %s:%u",
					g_tx_direct_mirror_rx_ip.c_str(), g_tx_direct_mirror_port);
			}

			/* 3. Dante DEP Audio Sink (skipped in direct RTP mode) */
			if (!direct_mode) {
				tx->tx_dep_sink = zst_dante_dep_audio_sink_create();
				set_str(tx->tx_dep_sink, "shm-name", "DanteEP");
				set_str(tx->tx_dep_sink, "channels", "0,1");
				set_str(tx->tx_dep_sink, "expected-sample-rate", "48000");
				set_uint(tx->tx_dep_sink, "queue-periods", 2048);

				/* 4. Audio Resampler (ASRC + 512-sample chunking) */
				tx->tx_resampler = zst_audio_resampler_create(48000, 2, "S32LE");
				set_str(tx->tx_resampler, "asrc-mode", "pts");
				set_str(tx->tx_resampler, "max-drift-ppm", "2000");
				set_uint(tx->tx_resampler, "drift-check-interval", 4);
				set_uint(tx->tx_resampler, "block-samples", 512);

				tx->tx_audio_pad = zst_element_get_pad(tx->tx_resampler, "sink");
				zst_pad_link(zst_element_get_pad(tx->tx_resampler, "src"),
				             zst_element_get_pad(tx->tx_dep_sink, "sink"));

				tx->app_audio_src_pad = zst_pad_create("app_audio_src", ZST_PAD_SRC);
				zst_pad_link(tx->app_audio_src_pad, tx->tx_audio_pad);
			}

			/* Add elements to pipeline */
			zst_pipeline_add(tx->pipe, tx->coordinator);
			if (tx->mirror_rtp_payloader) {
				zst_pipeline_add(tx->pipe, tx->mirror_rtp_payloader);
				zst_pipeline_add(tx->pipe, tx->mirror_udp_sink);
			}
			if (!direct_mode) {
				zst_pipeline_add(tx->pipe, tx->tx_resampler);
				zst_pipeline_add(tx->pipe, tx->tx_dep_sink);
			}

			tx->clock = zst_clock_system_create();
			zst_pipeline_set_clock(tx->pipe, tx->clock);
			zst_pipeline_set_state(tx->pipe, ZST_STATE_PLAYING);
			if (tx->session) {
				tx->session->bus = tx->pipe->bus;
				if (zst_element_set_state(tx->session, ZST_STATE_PLAYING) != ZST_OK) {
					LOGW("Dante session start failed (is /var/run/dante/dvr running?)");
				}
			}

			/* Scheduler */
			zst_scheduler_config_t cfg = {
				.mode = ZST_SCHEDULER_MULTI_THREAD,
				.worker_threads = 4
			};
			tx->sched = zst_scheduler_create(&cfg);
			zst_scheduler_attach(tx->sched, tx->pipe);
			pthread_create(&tx->sched_th, NULL, (void* (*)(void*))zst_scheduler_run, tx->sched);

			/* Direct mode: synthesize a unicast H.264 TX flow (no Dante daemon). */
			if (direct_mode) {
				zst_dante_flow_t flow = {};
				flow.direction = ZST_DANTE_FLOW_TX;
				flow.transport = ZST_DANTE_FLOW_UNICAST;
				flow.flow_index = 0;
				flow.channel_index = 0;
				flow.port = tx->direct_port;
				flow.receiver_address = (char*)(tx->direct_rx_ip.c_str());
				flow.transmitter_address = NULL;
				flow.multicast_address = NULL;
				snprintf(flow.video_subtype, sizeof(flow.video_subtype), "H264");
				zst_result_t r = zst_dante_video_coordinator_apply_flow(tx->coordinator, &flow);
				if (r != ZST_OK) {
					LOGE("direct TX flow apply failed, r=%d", (int)r);
				} else {
					LOGI("[DIRECT TX] unicasting H264 to %s:%u", tx->direct_rx_ip.c_str(),
					     tx->direct_port);
				}
			}
			tx->running = true;
			pthread_create(&tx->bus_th, NULL, dante_tx_bus_worker, tx);

			_FreeStack_ += [tx]() {
				LOGI("Stopping Dante TX pipeline (zstreamer)...");
				tx->running = false;
				if (tx->bus_th) {
					pthread_join(tx->bus_th, NULL);
					tx->bus_th = 0;
				}
				if (tx->app_video_src_pad) {
					zst_pad_unlink(tx->app_video_src_pad);
					zst_pad_destroy(tx->app_video_src_pad);
					tx->app_video_src_pad = nullptr;
				}
				if (tx->app_video_mirror_src_pad) {
					zst_pad_unlink(tx->app_video_mirror_src_pad);
					zst_pad_destroy(tx->app_video_mirror_src_pad);
					tx->app_video_mirror_src_pad = nullptr;
				}
				if (tx->app_audio_src_pad) {
					zst_pad_unlink(tx->app_audio_src_pad);
					zst_pad_destroy(tx->app_audio_src_pad);
					tx->app_audio_src_pad = nullptr;
				}
				if (tx->session) {
					zst_element_set_state(tx->session, ZST_STATE_NULL);
					zst_element_destroy(tx->session);
					tx->session = nullptr;
				}
				if (tx->sched) {
					zst_scheduler_stop(tx->sched);
					pthread_join(tx->sched_th, NULL);
					zst_scheduler_destroy(tx->sched);
					tx->sched = nullptr;
				}
				if (tx->pipe) {
					zst_pipeline_set_state(tx->pipe, ZST_STATE_NULL);
					zst_pipeline_destroy(tx->pipe);
					tx->pipe = nullptr;
				}
				if (tx->clock) {
					zst_clock_unref(tx->clock);
					tx->clock = nullptr;
				}
				struct timespec end_ts;
				clock_gettime(CLOCK_MONOTONIC, &end_ts);
				double duration = tx->start_time_ms > 0
					? ((uint64_t)end_ts.tv_sec * 1000 + end_ts.tv_nsec / 1000000 - tx->start_time_ms) / 1000.0
					: 0.0;
				uint64_t video_frames = tx->tx_video_frames.load();
				uint64_t video_bytes = tx->tx_video_bytes.load();
				uint64_t audio_frames = tx->tx_audio_frames.load();
				uint64_t audio_bytes = tx->tx_audio_bytes.load();
				uint64_t audio_samples = tx->tx_audio_samples.load();
				printf("\n================ Dante TXX Final Summary ================\n");
				printf("  Duration:            %.2f s\n", duration);
				printf("  Video Frames:        %llu (avg %.2f fps)\n",
					(unsigned long long)video_frames, duration > 0 ? video_frames / duration : 0.0);
				printf("  Video Data:          %.2f MB (avg %.2f Mbps)\n",
					(double)video_bytes / (1024.0 * 1024.0),
					duration > 0 ? video_bytes * 8.0 / (duration * 1000000.0) : 0.0);
				printf("  Audio Frames:        %llu\n", (unsigned long long)audio_frames);
				printf("  Audio Samples:       %llu (avg %.0f samples/s)\n",
					(unsigned long long)audio_samples, duration > 0 ? audio_samples / duration : 0.0);
				printf("  Audio Data:          %.2f MB (avg %.2f Mbps)\n",
					(double)audio_bytes / (1024.0 * 1024.0),
					duration > 0 ? audio_bytes * 8.0 / (duration * 1000000.0) : 0.0);
				printf("=========================================================\n\n");
				delete tx;
				LOGI("Dante TX pipeline stopped.");
			};

			*ppDanteTx = tx;
			return qres;
		}
	} mTestCase1;
};

int main(int argc, char* argv[]) {
	g_argc = argc;
	g_argv = argv;

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--direct") == 0 && i + 1 < argc) {
			g_tx_direct_rx_ip = argv[++i];
		} else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
			g_tx_direct_port = (uint16_t)strtoul(argv[++i], nullptr, 10);
		} else if (strcmp(argv[i], "--direct-mirror") == 0 && i + 1 < argc) {
			g_tx_direct_mirror_rx_ip = argv[++i];
		} else if (strcmp(argv[i], "--mirror-port") == 0 && i + 1 < argc) {
			g_tx_direct_mirror_port = (uint16_t)strtoul(argv[++i], nullptr, 10);
		} else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
			printf("Usage: sc6f0-dante-tx [--direct <rx-ip>] [--port <port>] [--direct-mirror <rx-ip>] [--mirror-port <port>] [--dump-stream <file>]\n");
			printf("  --direct <rx-ip>  Direct RTP mode: unicast H.264 to rx-ip:port,\n");
			printf("                    bypassing the Dante daemon (audio disabled)\n");
			printf("  --port <port>     RTP port (default: 16490)\n");
			printf("  --direct-mirror <rx-ip>  Add direct RTP alongside the Dante AVH flow\n");
			printf("  --mirror-port <port>     Direct mirror RTP port (default: 16491)\n");
			printf("  --dump-stream <f> Save the raw encoded Annex-B H.264 AU stream to <file>\n");
			return 0;
		} else if (strcmp(argv[i], "--dump-stream") == 0 && i + 1 < argc) {
			if (g_tx_dump)
				fclose(g_tx_dump);
			g_tx_dump = fopen(argv[++i], "wb");
			if (!g_tx_dump)
				fprintf(stderr, "sc6f0-dante-tx: cannot open %s\n", argv[i]);
			else
				printf("Dumping encoded Annex-B H.264 to %s\n", argv[i]);
		}
	}

	signal(SIGINT, tx_sig_handler);
	signal(SIGTERM, tx_sig_handler);

	int ret;
	switch(1) { case 1:
		modules_init_t _modules_init_;

		ret = App0::Main();
	}

	if (g_tx_dump) {
		fflush(g_tx_dump);
		fclose(g_tx_dump);
		g_tx_dump = NULL;
	}

	return ret;
}
