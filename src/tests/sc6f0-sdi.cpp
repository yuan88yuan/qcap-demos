#include <stdlib.h>

#include "qcap2.h"
#include "qcap2.drm.h"

#include "ZzLog.h"
#include "ZzModules.h"
#include "ZzClock.h"
#include "ZzStats.h"
#include "ZzUtils.h"
#include "testkit.h"

ZZ_INIT_LOG("sc6f0-sdi");

int g_argc = 0;
char** g_argv = NULL;

ZZ_MODULE_DECL(__zz_log__);

using namespace __zz_clock__;

namespace __sc6f0_sdi_demo__ {
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

using namespace __sc6f0_sdi_demo__;
using __testkit__::wait_for_test_finish;
using __testkit__::TestCase;
using __testkit__::free_stack_t;
using __testkit__::tick_ctrl_t;
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

	bool running;

	int Run() {
		int TEST_CASE;
		ZZ_ENV_INIT(TEST_CASE, 0);

		if(TEST_CASE != 0) {
			DoCase(TEST_CASE);

			return 0;
		}

		running = true;
		LOGI("Press 'q' to exit.");
		while(running) {
			char ch;

			int fd_stdin = 0; // stdin

			fd_set readfds;
			FD_ZERO(&readfds);

			int fd_max = -1;
			if(fd_stdin > fd_max) fd_max = fd_stdin;
			FD_SET(fd_stdin, &readfds);

			int err = select(fd_max + 1, &readfds, NULL, NULL, NULL);
			if (err < 0) {
				LOGE("%s(%d): select() failed! err = %d", __FUNCTION__, __LINE__, err);
				break;
			}

			if (FD_ISSET(fd_stdin, &readfds)) {
				ch = getchar();

				switch(ch) {
				case 'q':
					running = false;
					break;

				case '0':
				case '1':
				case '2':
				case '3':
				case '4':
				case '5':
				case '6':
				case '7':
				case '8':
				case '9':
					DoCase(ch - '0');
					break;
				}
			}
		}

		return 0;
	}

	void DoCase(int t) {
		LOGD("DoCase(%d)", t);

		switch(t) {
		case 1:
			mTestCase1.DoWork();
			break;
		}
	}

	struct TestCase1 : public TestCase {
	typedef TestCase1 self_t;
	typedef TestCase super_t;

	int nSnapshot = 0;
	bool bPause = false;
	free_stack_t _FreeStack_vsrc_;

	void DoWork() {
			QRESULT qres;

			LOGD("%s::%s", typeid(self_t).name(), __FUNCTION__);

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

				wait_for_test_finish([&](int ch) -> bool {
					QRESULT qres;

					switch(1) { case 1:
						switch(ch) {
						case 's':
						case 'S':
							nSnapshot++;
							LOGI("Snapshot requested. Pending: %d", nSnapshot);
							break;

						case 'p':
						case 'P':
							bPause = !bPause;
							LOGI("Pause: %d", (int)bPause);
							break;
						}
						break;
					}

					return true;
				}, 1000000LL, 10LL);
			}

			_FreeStack_main_.flush();
		}

		QRETURN OnStart(free_stack_t& _FreeStack_, QRESULT& qres) {
			int err;

			switch(1) { case 1:
				qcap2_event_t* pDmxEvent;
				qcap2_demuxer_t* pDmx;
				qres = StartDmx(_FreeStack_, &pDmx, &pDmxEvent);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): StartDmx() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				qres = AddEventHandler(_FreeStack_, pDmxEvent, std::bind(&self_t::OnDmx, this, pDmx));
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): AddEventHandler() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				_FreeStack_ += [&]() {
					_FreeStack_vsrc_.flush();
				};
			}

			return QCAP_RT_OK;
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

		QRETURN OnDmx(qcap2_demuxer_t* pDmx) {
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
				const ULONG nColorSpaceType = QCAP_COLORSPACE_TYPE_XV20;
				const ULONG nVideoWidth = 3840;
				const ULONG nVideoHeight = 2160;
				const ULONG nVideoEncoderFormat = QCAP_ENCODER_FORMAT_H264;
				const ULONG nVideoBitRate = 10 * 1000000;
				const ULONG nVsinkColorSpaceType = QCAP_COLORSPACE_TYPE_XV20;
				const ULONG nVsinkVideoWidth = 3840;
				const ULONG nVsinkVideoHeight = 2160;
				ULONG nSrcVideoWidth = 0;
				ULONG nSrcVideoHeight = 0;
				BOOL bVideoIsInterleaved;
				double dVideoFrameRate;
				ULONG nAudioChannels = 0;
				ULONG nAudioBitsPerSample = 0;
				ULONG nAudioSampleFrequency = 0;
				const ULONG nAudioEncoderFormat = QCAP_ENCODER_FORMAT_AAC_ADTS;

				qcap2_event_t* pVsrcEvent = NULL;
				qcap2_event_t* pAsrcEvent = NULL;
				qcap2_video_sink_t* pVsink = NULL;
				qcap2_video_sink_t* pVsink1 = NULL;

				qcap2_video_source_t* pVsrc = NULL;
				if(nVideoIndex >= 0) {
					pVsrc = qcap2_demuxer_get_video_source(pDmx, nVideoIndex);
				}
				if(pVsrc) {
					std::shared_ptr<qcap2_video_format_t> pVideoFormat(qcap2_video_format_new(), qcap2_video_format_delete);
					qcap2_video_source_get_video_format(pVsrc, pVideoFormat.get());
					{
						qcap2_video_format_get_property(pVideoFormat.get(), &nSrcColorSpaceType, &nSrcVideoWidth, &nSrcVideoHeight, &bVideoIsInterleaved, &dVideoFrameRate);

						if(nSrcColorSpaceType == QCAP_COLORSPACE_TYPE_UNDEFINED || nSrcVideoWidth <= 0 || nSrcVideoHeight <= 0) {
							LOGI("v: no-link");
						} else {
							LOGI("v: %08X %ux%u'%u, %.2f", nSrcColorSpaceType, nSrcVideoWidth, nSrcVideoHeight, bVideoIsInterleaved, dVideoFrameRate);

	#if 1
							qres = StartVsrc(_FreeStack_, pVsrc, nColorSpaceType,
								nVideoWidth, nVideoHeight, bVideoIsInterleaved, dVideoFrameRate, &pVsrcEvent);
							if(qres != QCAP_RS_SUCCESSFUL) {
								LOGE("%s(%d): StartVsrc() failed, qres=%d", __FUNCTION__, __LINE__, qres);
								break;
							}
	#endif

	#if 1
							qres = StartVsink(_FreeStack_, 36, nVsinkColorSpaceType,
								nVsinkVideoWidth, nVsinkVideoHeight, bVideoIsInterleaved, dVideoFrameRate, &pVsink);
							if(qres != QCAP_RS_SUCCESSFUL) {
								LOGE("%s(%d): StartVsink() failed, qres=%d", __FUNCTION__, __LINE__, qres);
								break;
							}
	#endif

	#if 1
							qres = StartVsink_overlay(_FreeStack_, 34, nColorSpaceType,
								nVideoWidth, nVideoHeight, bVideoIsInterleaved, dVideoFrameRate, &pVsink1);
							if(qres != QCAP_RS_SUCCESSFUL) {
								LOGE("%s(%d): StartVsink_overlay() failed, qres=%d", __FUNCTION__, __LINE__, qres);
								break;
							}
	#endif

						}
					}
				} else {
					LOGI("v: no-link");
				}

				qcap2_audio_source_t* pAsrc = NULL;
				if(nAudioIndex >= 0) {
					pAsrc = qcap2_demuxer_get_audio_source(pDmx, nAudioIndex);
				}
				if(pAsrc) {
					std::shared_ptr<qcap2_audio_format_t> pAudioFormat(qcap2_audio_format_new(), qcap2_audio_format_delete);
					qcap2_audio_source_get_audio_format(pAsrc, pAudioFormat.get());
					{
						qcap2_audio_format_get_property(pAudioFormat.get(), &nAudioChannels, &nAudioBitsPerSample, &nAudioSampleFrequency);

						if(nAudioChannels == 0 || nAudioBitsPerSample == 0 || nAudioSampleFrequency == 0) {
							LOGI("a: no-link");
						} else {
							LOGI("a: %ux%u'%u", nAudioChannels, nAudioBitsPerSample, nAudioSampleFrequency);

#if 1
							qres = StartAsrc(_FreeStack_, pAsrc, &pAsrcEvent);
							if(qres != QCAP_RS_SUCCESSFUL) {
								LOGE("%s(%d): StartAsrc() failed, qres=%d", __FUNCTION__, __LINE__, qres);
								break;
							}
#endif
						}
					}
				} else {
					LOGI("a: no-link");
				}

				qcap2_rcbuffer_t* pRCBuffer_overlay = NULL;
#if 0
				{
					qcap2_rcbuffer_t* pRCBuffer;
					qres = __testkit__::new_video_qdmabuf(_FreeStack_, 1920, 1080, QCAP_COLORSPACE_TYPE_RGBA, PROT_WRITE | PROT_READ, &pRCBuffer);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): new_video_qdmabuf() failed, qres=%d", __FUNCTION__, __LINE__, qres);
						break;
					}

					qres = __testkit__::map_video_qdmabuf(pRCBuffer, PROT_WRITE | PROT_READ);
					ZzUtils::Scoped ZZ_GUARD_NAME([pRCBuffer]() {
						__testkit__::unmap_video_qdmabuf(pRCBuffer);
					});

					qcap2_dmabuf_t* pDmabuf;
					qres = qcap2_rcbuffer_get_qdmabuf(pRCBuffer, &pDmabuf);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): qcap2_rcbuffer_get_qdmabuf() failed, qres=%d", __FUNCTION__, __LINE__, qres);
						break;
					}

					std::shared_ptr<qcap2_av_frame_t> pAVFrame(
						(qcap2_av_frame_t*)qcap2_rcbuffer_lock_data(pRCBuffer),
						[pRCBuffer](qcap2_av_frame_t*) {
							qcap2_rcbuffer_unlock_data(pRCBuffer);
						});

					uint8_t* pData[4];
					int pLineSize[4];
					qcap2_av_frame_get_buffer1(pAVFrame.get(), pData, pLineSize);

					LOGD("fd=%d [%p %p %p %p] [%d,%d,%d,%d]", pDmabuf->fd,
						pData[0], pData[1], pData[2], pData[3],
						(int)pLineSize[0], (int)pLineSize[1], (int)pLineSize[2], (int)pLineSize[3]);

					pRCBuffer_overlay = pRCBuffer;
					qcap2_rcbuffer_add_ref(pRCBuffer_overlay);

					_FreeStack_ += [pRCBuffer_overlay]() {
						qcap2_rcbuffer_release(pRCBuffer_overlay);
					};
				}
#endif

				// next level muxers
				if(nSrcColorSpaceType != QCAP_COLORSPACE_TYPE_UNDEFINED && nSrcVideoWidth > 0 && nSrcVideoHeight > 0) {
					if(pVsrcEvent) {
						qres = AddEventHandler(_FreeStack_, pVsrcEvent,
							std::bind(&self_t::OnVsrc, this, pVsrc, pVsink, pVsink1, pRCBuffer_overlay));
						if(qres != QCAP_RS_SUCCESSFUL) {
							LOGE("%s(%d): AddEventHandler() failed, qres=%d", __FUNCTION__, __LINE__, qres);
							break;
						}
					}
				}

				if(pAsrcEvent) {
					qres = AddEventHandler(_FreeStack_, pAsrcEvent,
						std::bind(&self_t::OnAsrc, this, pAsrc));
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): AddEventHandler() failed, qres=%d", __FUNCTION__, __LINE__, qres);
						break;
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

		QRETURN OnVsrc(qcap2_video_source_t* pVsrc, qcap2_video_sink_t* pVsink, qcap2_video_sink_t* pVsink1, qcap2_rcbuffer_t* pRCBuffer_overlay) {
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

				if(nSnapshot > 0) {
					nSnapshot--;
					qres = qcap2_save_raw_video_frame(pRCBuffer, "snapshot");
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): qcap2_save_raw_video_frame() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					} else {
						LOGI("Snapshot saved successfully.");
					}
				}

				if(pVsink && ! bPause) {
					qres = qcap2_video_sink_push(pVsink, pRCBuffer);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): qcap2_video_sink_push() failed, qres=%d", __FUNCTION__, __LINE__, qres);
						break;
					}
				}

				if(pVsink1 && pRCBuffer_overlay) {
					qres = qcap2_video_sink_push(pVsink1, pRCBuffer_overlay);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): qcap2_video_sink_push() failed, qres=%d", __FUNCTION__, __LINE__, qres);
						break;
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

		QRETURN OnAsrc(qcap2_audio_source_t* pAsrc) {
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
			}

			return QCAP_RT_OK;
		}

		QRESULT StartVsink(free_stack_t& _FreeStack_, uint32_t nDrmPlaneId, ULONG nColorSpaceType,
			ULONG nVideoWidth, ULONG nVideoHeight, BOOL bVideoIsInterleaved, double dVideoFrameRate, qcap2_video_sink_t** ppVsink) {
			QRESULT qres = QCAP_RS_SUCCESSFUL;

			switch(1) { case 1:
				qcap2_video_sink_t* pVsink = qcap2_video_sink_new();
				_FreeStack_ += [pVsink]() {
					qcap2_video_sink_delete(pVsink);
				};

				qcap2_video_sink_set_backend_type(pVsink, QCAP2_VIDEO_SINK_BACKEND_TYPE_XLNX2);
				qcap2_video_sink_set_native_handle(pVsink, QCAP_HWND_DRM_PLANE_ID_MASK | nDrmPlaneId);

				{
					std::shared_ptr<qcap2_video_format_t> pVideoFormat(
						qcap2_video_format_new(), qcap2_video_format_delete);

					qcap2_video_format_set_property(pVideoFormat.get(),
						nColorSpaceType, nVideoWidth, nVideoHeight, FALSE, 60);

					qcap2_video_sink_set_video_format(pVsink, pVideoFormat.get());
				}

				qres = qcap2_video_sink_start(pVsink);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_video_sink_start() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
				_FreeStack_ += [pVsink]() {
					QRESULT qres;

					qres = qcap2_video_sink_stop(pVsink);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): qcap2_video_sink_start() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					}
				};

				*ppVsink = pVsink;
			}

			return qres;
		}

		QRESULT StartVsink_overlay(free_stack_t& _FreeStack_, uint32_t nDrmPlaneId, ULONG nColorSpaceType,
			ULONG nVideoWidth, ULONG nVideoHeight, BOOL bVideoIsInterleaved, double dVideoFrameRate, qcap2_video_sink_t** ppVsink) {
			QRESULT qres = QCAP_RS_SUCCESSFUL;

			switch(1) { case 1:
				qcap2_video_sink_t* pVsink = qcap2_video_sink_new();
				_FreeStack_ += [pVsink]() {
					qcap2_video_sink_delete(pVsink);
				};

				qcap2_video_sink_set_backend_type(pVsink, QCAP2_VIDEO_SINK_BACKEND_TYPE_XLNX2);
				qcap2_video_sink_set_native_handle(pVsink, QCAP_HWND_DRM_PLANE_ID_MASK | nDrmPlaneId);

				{
					std::shared_ptr<qcap2_video_format_t> pVideoFormat(
						qcap2_video_format_new(), qcap2_video_format_delete);

					qcap2_video_format_set_property(pVideoFormat.get(),
						nColorSpaceType, nVideoWidth, nVideoHeight, FALSE, 60);

					qcap2_video_sink_set_video_format(pVsink, pVideoFormat.get());
				}

				qcap2_video_sink_set_drm_alpha(pVsink, 128);

				qres = qcap2_video_sink_start(pVsink);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_video_sink_start() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
				_FreeStack_ += [pVsink]() {
					QRESULT qres;

					qres = qcap2_video_sink_stop(pVsink);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): qcap2_video_sink_start() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					}
				};

				*ppVsink = pVsink;
			}

			return qres;
		}
	} mTestCase1;
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
