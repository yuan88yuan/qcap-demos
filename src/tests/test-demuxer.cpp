#include <stdlib.h>
#include <time.h>

#include "qcap2.h"
#include "qcap2.v4l2.h"
#include "qcap2.allegro2.h"

#include "ZzLog.h"
#include "ZzModules.h"
#include "ZzClock.h"
#include "ZzStats.h"
#include "ZzUtils.h"
#include "testkit.h"

ZZ_INIT_LOG("test-demuxer");

int g_argc = 0;
char** g_argv = NULL;

ZZ_MODULE_DECL(__zz_log__);

using namespace __zz_clock__;

namespace __test_demuxer__ {
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

using namespace __test_demuxer__;
using __testkit__::wait_for_test_finish;
using __testkit__::TestCase;
using __testkit__::free_stack_t;
using __testkit__::tick_ctrl_t;
using __testkit__::NewEvent;
using __testkit__::AddEventHandler;

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

		case 2:
			mTestCase2.DoWork();
			break;
		}
	}

	struct TestCase1 : public TestCase {
		typedef TestCase1 self_t;
		typedef TestCase super_t;

		free_stack_t _FreeStack_dmx_;
		free_stack_t _FreeStack_dmx_evt_;

		int nSnapshot;

		void DoWork() {
			QRESULT qres;

			LOGD("%s::%s", typeid(self_t).name(), __FUNCTION__);

			nSnapshot = 0;

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

					switch(ch) {
					case 's': case 'S':
						LOGD("++nSnapshot=%d", ++nSnapshot);
						break;

					case 'd': case 'D': {
						QRESULT qres_evt = QCAP_RS_SUCCESSFUL;
						qres = ExecInEventHandlers(std::bind(&self_t::StartDmx, this, std::ref(qres_evt)));
						if(qres != QCAP_RS_SUCCESSFUL) {
							LOGE("%s(%d): ExecInEventHandlers() failed, qres=%d", __FUNCTION__, __LINE__, qres);
							break;
						}
						if(qres_evt != QCAP_RS_SUCCESSFUL) {
							break;
						}
					}
						break;
					}

					return true;
				}, 1000000LL, 10LL);
			}

			_FreeStack_main_.flush();
		}

		QRETURN OnStart(free_stack_t& _FreeStack_, QRESULT& qres) {
			switch(1) { case 1:
				StartDmx(qres);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): StartDmx() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				_FreeStack_ += [&]() {
					_FreeStack_dmx_.flush();
				};
			}

			return QCAP_RT_OK;
		}

		QRETURN StartDmx(QRESULT& qres) {
			free_stack_t& _FreeStack_ = _FreeStack_dmx_;
			_FreeStack_.flush();

			switch(1) { case 1:
				qcap2_event_t* pDmxEvent;
				qcap2_demuxer_t* pDmx;
				qres = StartDmxCtx(_FreeStack_, &pDmx, &pDmxEvent);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): StartDmx() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				qres = AddEventHandler(_FreeStack_, pDmxEvent, std::bind(&self_t::OnDmx, this, pDmx));
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): AddEventHandler() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
			}

			return QCAP_RT_OK;
		}

		QRESULT StartDmxCtx(free_stack_t& _FreeStack_, qcap2_demuxer_t** ppDmx, qcap2_event_t** ppDmxEvent) {
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

				qcap2_demuxer_set_type(pDmx, QCAP2_DEMUXER_TYPE_DEFAULT);
				qcap2_demuxer_set_event(pDmx, pEvent);
				qcap2_demuxer_set_live_source(pDmx, false);

				std::string strURL("file:///mnt/dev/zzlee/docker/tmp/test.mp4");
				qcap2_demuxer_set_url(pDmx, strURL.c_str());

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

				_FreeStack_ += [&]() {
					_FreeStack_dmx_evt_.flush();
				};

				*ppDmx = pDmx;
				*ppDmxEvent = pEvent;
			}

			return qres;
		}

		QRETURN OnDmx(qcap2_demuxer_t* pDmx) {
			QRESULT qres;

			switch(1) {	case 1:
				free_stack_t& _FreeStack_ = _FreeStack_dmx_evt_;
				_FreeStack_.flush();


				qres = qcap2_demuxer_update(pDmx);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_demuxer_update() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				LOGD("%d VENCs, %d AENCs, %d PROGs",
					qcap2_demuxer_get_video_encoder_count(pDmx),
					qcap2_demuxer_get_audio_encoder_count(pDmx),
					qcap2_demuxer_get_program_count(pDmx));

				qcap2_program_info_t* pProgram = qcap2_demuxer_get_program_info(pDmx, 0);
				int nVencIndex = qcap2_program_info_get_video_encoder_index(pProgram, 0);
				int nAencIndex = qcap2_program_info_get_audio_encoder_index(pProgram, 0);

				LOGD("Prog[%s/%s] Venc:%d, Aenc:%d", qcap2_program_info_get_metadata(pProgram, "service_name"),
					qcap2_program_info_get_metadata(pProgram, "service_provider"), nVencIndex, nAencIndex);

				qcap2_video_encoder_t* pVenc = qcap2_demuxer_get_video_encoder(pDmx, nVencIndex);
				qcap2_video_decoder_t* pVdec;
				qcap2_event_t* pVdecEvent;
				{
					std::shared_ptr<qcap2_video_encoder_property_t> pVencProp(qcap2_video_encoder_property_new(), qcap2_video_encoder_property_delete);
					qcap2_video_encoder_get_video_property(pVenc, pVencProp.get());

					ULONG nEncoderFormat, nWidth, nHeight;
					qcap2_video_encoder_property_get_format(pVencProp.get(), &nEncoderFormat);
					qcap2_video_encoder_property_get_resolution(pVencProp.get(), &nWidth, &nHeight);

					uint8_t* pExtraData;
					int nExtraDataSize;
					qcap2_video_encoder_get_extra_data(pVenc, &pExtraData, &nExtraDataSize);

					LOGD("%08X ' %dx%d, extradata(%d)", nEncoderFormat, nWidth, nHeight, nExtraDataSize);

					qcap2_video_encoder_property_set_type(pVencProp.get(), QCAP_ENCODER_TYPE_ALLEGRO2);

					qres = StartVdec(_FreeStack_, pVencProp.get(), pExtraData, nExtraDataSize, &pVdec, &pVdecEvent);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): StartVdec() failed, qres=%d", __FUNCTION__, __LINE__, qres);
						break;
					}
				}

				qcap2_video_sink_t* pVsink = NULL;

#if 0
				qres = StartVsink(_FreeStack_, &pVsink);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): StartVsink() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
#endif

				qcap2_event_t* pVencEvent;
				qres = StartDmx_Venc(_FreeStack_, pVenc, &pVencEvent);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): StartDmx_Venc() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				qcap2_audio_encoder_t* pAenc = qcap2_demuxer_get_audio_encoder(pDmx, nAencIndex);
				qcap2_audio_decoder_t* pAdec;
				qcap2_event_t* pAdecEvent;
				{
					std::shared_ptr<qcap2_audio_encoder_property_t> pAencProp(qcap2_audio_encoder_property_new(), qcap2_audio_encoder_property_delete);
					qcap2_audio_encoder_get_audio_property(pAenc, pAencProp.get());

					ULONG nEncoderType, nEncoderFormat, nChannels, nBitsPerSample, nSampleFrequency;
					qcap2_audio_encoder_property_get_property(pAencProp.get(), &nEncoderType, &nEncoderFormat, &nChannels, &nBitsPerSample, &nSampleFrequency);

					uint8_t* pExtraData;
					int nExtraDataSize;
					qcap2_audio_encoder_get_extra_data(pAenc, &pExtraData, &nExtraDataSize);

					LOGD("%08X ' %dx%dx%d ' extradata(%d)", nEncoderFormat, nChannels, nBitsPerSample, nSampleFrequency, nExtraDataSize);

					qcap2_audio_encoder_property_set_type(pAencProp.get(), QCAP_ENCODER_TYPE_SOFTWARE);
					qres = StartAdec(_FreeStack_, pAencProp.get(), pExtraData, nExtraDataSize, &pAdec, &pAdecEvent);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): StartAdec() failed, qres=%d", __FUNCTION__, __LINE__, qres);
						break;
					}
				}

				qcap2_event_t* pAencEvent;
				qres = StartDmx_Aenc(_FreeStack_, pAenc, &pAencEvent);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): StartDmx_Aenc() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				qres = AddEventHandler(_FreeStack_, pVencEvent, std::bind(&self_t::OnDmx_Venc, this, pVenc, pVdec));
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): AddEventHandler() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				qres = AddEventHandler(_FreeStack_, pAencEvent, std::bind(&self_t::OnDmx_Aenc, this, pAenc, pAdec));
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): AddEventHandler() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				qres = AddEventHandler(_FreeStack_, pVdecEvent, std::bind(&self_t::OnVdec, this, pVdec, pVsink));
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): AddEventHandler() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				qres = AddEventHandler(_FreeStack_, pAdecEvent, std::bind(&self_t::OnAdec, this, pAdec));
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): AddEventHandler() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				qres = qcap2_demuxer_play(pDmx);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_demuxer_play() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
			}

			return QCAP_RT_OK;
		}

		QRESULT StartDmx_Venc(free_stack_t& _FreeStack_, qcap2_video_encoder_t* pVenc, qcap2_event_t** ppVencEvent) {
			QRESULT qres;

			switch(1) { case 1:
				qcap2_event_t* pEvent;
				qres = NewEvent(_FreeStack_, &pEvent);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): NewEvent() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				qcap2_video_encoder_set_event(pVenc, pEvent);

				qres = qcap2_video_encoder_start(pVenc);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGD("%s(%d): qcap2_video_encoder_start() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
				_FreeStack_ += [pVenc]() {
					QRESULT qres;

					qres = qcap2_video_encoder_stop(pVenc);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGD("%s(%d): qcap2_video_encoder_stop() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					}
				};

				*ppVencEvent = pEvent;
			}

			return qres;
		}

		QRESULT StartDmx_Aenc(free_stack_t& _FreeStack_, qcap2_audio_encoder_t* pAenc, qcap2_event_t** ppAencEvent) {
			QRESULT qres;

			switch(1) { case 1:
				qcap2_event_t* pEvent;
				qres = NewEvent(_FreeStack_, &pEvent);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): NewEvent() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				qcap2_audio_encoder_set_event(pAenc, pEvent);

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

				*ppAencEvent = pEvent;
			}

			return qres;
		}

		QRESULT StartVdec(free_stack_t& _FreeStack_, qcap2_video_encoder_property_t* pVencProp, uint8_t* pExtraData, int nExtraDataSize, qcap2_video_decoder_t** ppVdec, qcap2_event_t** ppVdecEvent) {
			QRESULT qres;

			switch(1) { case 1:
				qcap2_event_t* pEvent;
				qres = NewEvent(_FreeStack_, &pEvent);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): NewEvent() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				qcap2_video_decoder_t* pVdec = qcap2_video_decoder_new();
				_FreeStack_ += [pVdec]() {
					qcap2_video_decoder_delete(pVdec);
				};

				qcap2_video_decoder_set_video_property(pVdec, pVencProp);
				qcap2_video_decoder_set_event(pVdec, pEvent);
				qcap2_video_decoder_set_extra_data(pVdec, pExtraData, nExtraDataSize);

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

				*ppVdec = pVdec;
				*ppVdecEvent = pEvent;
			}

			return qres;
		}

		QRESULT StartVsink(free_stack_t& _FreeStack_, qcap2_video_sink_t** ppVsink) {
			QRESULT qres = QCAP_RS_SUCCESSFUL;

			switch(1) { case 1:
				const int nBuffers = 4;
				const ULONG nColorSpaceType = QCAP_COLORSPACE_TYPE_NV12;
				const ULONG nVideoFrameWidth = 3840;
				const ULONG nVideoFrameHeight = 2160;

				qcap2_video_sink_t* pVsink = qcap2_video_sink_new();
				_FreeStack_ += [pVsink]() {
					qcap2_video_sink_delete(pVsink);
				};

				qcap2_video_sink_set_backend_type(pVsink, QCAP2_VIDEO_SINK_BACKEND_TYPE_XLNX2);
				const uint32_t nDrmPlaneId = 34;
				qcap2_video_sink_set_native_handle(pVsink, QCAP_HWND_DRM_PLANE_ID_MASK | nDrmPlaneId);

				{
					std::shared_ptr<qcap2_video_format_t> pVideoFormat(
						qcap2_video_format_new(), qcap2_video_format_delete);

					qcap2_video_format_set_property(pVideoFormat.get(),
						nColorSpaceType, nVideoFrameWidth, nVideoFrameHeight, FALSE, 60);

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

		QRESULT StartAdec(free_stack_t& _FreeStack_, qcap2_audio_encoder_property_t* pAencProp, uint8_t* pExtraData, int nExtraDataSize, qcap2_audio_decoder_t** ppAdec, qcap2_event_t** ppAdecEvent) {
			QRESULT qres;

			switch(1) { case 1:
				qcap2_event_t* pEvent;
				qres = NewEvent(_FreeStack_, &pEvent);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): NewEvent() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				qcap2_audio_decoder_t* pAdec = qcap2_audio_decoder_new();
				_FreeStack_ += [pAdec]() {
					qcap2_audio_decoder_delete(pAdec);
				};

				qcap2_audio_decoder_set_audio_property(pAdec, pAencProp);
				qcap2_audio_decoder_set_event(pAdec, pEvent);
				qcap2_audio_decoder_set_extra_data(pAdec, pExtraData, nExtraDataSize);

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

				*ppAdec = pAdec;
				*ppAdecEvent = pEvent;
			}

			return qres;
		}

		QRETURN OnDmx_Venc(qcap2_video_encoder_t* pVenc, qcap2_video_decoder_t* pVdec) {
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

				qres = qcap2_video_decoder_push(pVdec, pRCBuffer);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_video_decoder_push() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
			}

			return QCAP_RT_OK;
		}

		QRETURN OnDmx_Aenc(qcap2_audio_encoder_t* pAenc, qcap2_audio_decoder_t* pAdec) {
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

				qres = qcap2_audio_decoder_push(pAdec, pRCBuffer);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_audio_decoder_push() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
			}

			return QCAP_RT_OK;
		}

		QRETURN OnVdec(qcap2_video_decoder_t* pVdec, qcap2_video_sink_t* pVsink) {
			QRESULT qres;

			switch(1) { case 1:
				qcap2_rcbuffer_t* pRCBuffer;
				qres = qcap2_video_decoder_pop(pVdec, &pRCBuffer);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_video_decoder_pop() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
				std::shared_ptr<qcap2_rcbuffer_t> pRCBuffer_(pRCBuffer,
					qcap2_rcbuffer_release);

				if(pVsink) {
					qres = qcap2_video_sink_push(pVsink, pRCBuffer);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): qcap2_video_sink_push() failed, qres=%d", __FUNCTION__, __LINE__, qres);
						break;
					}
				}
			}

			return QCAP_RT_OK;
		}

		QRETURN OnAdec(qcap2_audio_decoder_t* pAdec) {
			QRESULT qres;

			switch(1) { case 1:
				qcap2_rcbuffer_t* pRCBuffer;
				qres = qcap2_audio_decoder_pop(pAdec, &pRCBuffer);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_audio_decoder_pop() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
				std::shared_ptr<qcap2_rcbuffer_t> pRCBuffer_(pRCBuffer,
					qcap2_rcbuffer_release);
			}

			return QCAP_RT_OK;
		}
	} mTestCase1;

	struct TestCase2 : public TestCase {
		typedef TestCase2 self_t;
		typedef TestCase super_t;

		int nSnapshot;

		void DoWork() {
			QRESULT qres;

			LOGD("%s::%s", typeid(self_t).name(), __FUNCTION__);

			nSnapshot = 0;

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

					switch(ch) {
					case 's': case 'S':
						LOGD("++nSnapshot=%d", ++nSnapshot);
						break;
					}

					return true;
				}, 1000000LL, 10LL);
			}

			_FreeStack_main_.flush();
		}

		QRETURN OnStart(free_stack_t& _FreeStack_, QRESULT& qres) {
			switch(1) { case 1:
				qcap2_video_sink_t* pVsink;
				qres = StartVsink(_FreeStack_, &pVsink);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): StartVsink() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
			}

			return QCAP_RT_OK;
		}

		QRESULT StartVsink(free_stack_t& _FreeStack_, qcap2_video_sink_t** ppVsink) {
			QRESULT qres = QCAP_RS_SUCCESSFUL;

			switch(1) { case 1:
				const int nBuffers = 4;
				const ULONG nColorSpaceType = QCAP_COLORSPACE_TYPE_NV12;
				const ULONG nVideoFrameWidth = 1920;
				const ULONG nVideoFrameHeight = 1080;

				qcap2_video_sink_t* pVsink = qcap2_video_sink_new();
				_FreeStack_ += [pVsink]() {
					qcap2_video_sink_delete(pVsink);
				};

				qcap2_video_sink_set_backend_type(pVsink, QCAP2_VIDEO_SINK_BACKEND_TYPE_V4L2);
				qcap2_video_sink_set_v4l2_name(pVsink, "video0");
				qcap2_video_sink_set_buf_type(pVsink, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
				qcap2_video_sink_set_memory(pVsink, V4L2_MEMORY_DMABUF);
				// qcap2_video_sink_set_auto_run(pVsink, false);

				{
					std::shared_ptr<qcap2_video_format_t> pVideoFormat(
						qcap2_video_format_new(), qcap2_video_format_delete);

					qcap2_video_format_set_property(pVideoFormat.get(),
						nColorSpaceType, nVideoFrameWidth, nVideoFrameHeight, FALSE, 60);

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
