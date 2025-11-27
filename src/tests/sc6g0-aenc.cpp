#include <stdlib.h>
#include <time.h>

#include "qcap2.h"
#include "qcap2.nvt.hdal.h"

#include "ZzLog.h"
#include "ZzModules.h"
#include "ZzClock.h"
#include "ZzStats.h"
#include "ZzUtils.h"
#include "testkit.h"

ZZ_INIT_LOG("sc6g-aenc");

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

namespace __sc6g0_aenc__ {
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

	QRESULT StartFakeAsrcQ(free_stack_t& _FreeStack_, qcap2_event_handlers_t* pEventHandlers, qcap2_event_t* pEvent, qcap2_rcbuffer_queue_t** ppAsrcQ) {
		QRESULT qres = QCAP_RS_SUCCESSFUL;

		switch(1) { case 1:
			const int nBuffers = 10;
			const ULONG nChannels = 2;
			const ULONG nSampleFmt = QCAP_SAMPLE_FMT_S16;
			const ULONG nSampleFrequency = 48000;
			const ULONG nFrameSize = 960;

			qcap2_rcbuffer_t** pRCBuffers = new qcap2_rcbuffer_t*[nBuffers];
			_FreeStack_ += [pRCBuffers]() {
				delete[] pRCBuffers;
			};
			for(int i = 0;i < nBuffers;i++) {
				qcap2_rcbuffer_t* pRCBuffer;
				qres = __testkit__::new_audio_sysbuf(_FreeStack_, nChannels, nSampleFmt, nSampleFrequency, nFrameSize, &pRCBuffer);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): __testkit__::new_audio_sysbuf() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				pRCBuffers[i] = pRCBuffer;
			}
			if(qres != QCAP_RS_SUCCESSFUL)
				break;

			qcap2_rcbuffer_queue_t* pBufferQ = qcap2_rcbuffer_queue_new();
			_FreeStack_ += [pBufferQ]() {
				qcap2_rcbuffer_queue_delete(pBufferQ);
			};

			qcap2_rcbuffer_queue_set_max_buffers(pBufferQ, nBuffers);
			qcap2_rcbuffer_queue_set_buffers(pBufferQ, pRCBuffers);

			qres = qcap2_rcbuffer_queue_start(pBufferQ);
			if(qres != QCAP_RS_SUCCESSFUL) {
				LOGE("%s(%d): qcap2_rcbuffer_queue_start() failed, qres=%d", __FUNCTION__, __LINE__, qres);
				break;
			}
			_FreeStack_ += [pBufferQ]() {
				QRESULT qres;

				qres = qcap2_rcbuffer_queue_stop(pBufferQ);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_rcbuffer_queue_stop() failed, qres=%d", __FUNCTION__, __LINE__, qres);
				}
			};

			qcap2_rcbuffer_queue_t* pAsrcQ = qcap2_rcbuffer_queue_new();
			_FreeStack_ += [pAsrcQ]() {
				qcap2_rcbuffer_queue_delete(pAsrcQ);
			};

			qcap2_rcbuffer_queue_set_max_buffers(pAsrcQ, nBuffers);
			qcap2_rcbuffer_queue_set_event(pAsrcQ, pEvent);

			qres = qcap2_rcbuffer_queue_start(pAsrcQ);
			if(qres != QCAP_RS_SUCCESSFUL) {
				LOGE("%s(%d): qcap2_rcbuffer_queue_start() failed, qres=%d", __FUNCTION__, __LINE__, qres);
				break;
			}
			_FreeStack_ += [pAsrcQ]() {
				QRESULT qres;

				qres = qcap2_rcbuffer_queue_stop(pAsrcQ);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_rcbuffer_queue_stop() failed, qres=%d", __FUNCTION__, __LINE__, qres);
				}
			};

			tick_ctrl_t* pTickCtrl = new tick_ctrl_t();
			_FreeStack_ += [pTickCtrl]() {
				delete pTickCtrl;
			};

			pTickCtrl->num = 50 * 1000LL;
			pTickCtrl->den = 1000LL;

			qcap2_timer_t* pTimer = qcap2_timer_new();
			_FreeStack_ += [pTimer]() {
				qcap2_timer_delete(pTimer);
			};

			qres = qcap2_timer_start(pTimer);
			if(qres != QCAP_RS_SUCCESSFUL) {
				LOGE("%s(%d): qcap2_timer_start() failed, qres=%d", __FUNCTION__, __LINE__, qres);
				break;
			}
			_FreeStack_ += [pTimer]() {
				QRESULT qres;

				qres = qcap2_timer_stop(pTimer);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_timer_stop() failed, qres=%d", __FUNCTION__, __LINE__, qres);
				}
			};

			qres = __testkit__::AddTimerHandler(_FreeStack_, pEventHandlers, pTimer, [pTimer, pTickCtrl, pBufferQ, pAsrcQ]() -> QRETURN {
				QRESULT qres;
				int64_t now = _clk();

				switch(1) { case 1:
					uint64_t nExpirations;
					qres = qcap2_timer_wait(pTimer, &nExpirations);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): qcap2_timer_wait() failed, qres=%d", __FUNCTION__, __LINE__, qres);
						break;
					}

					qres = qcap2_timer_next(pTimer, pTickCtrl->advance(now));
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): qcap2_timer_next() failed, qres=%d", __FUNCTION__, __LINE__, qres);
						break;
					}

					qcap2_rcbuffer_t* pRCBuffer;
					qres = qcap2_rcbuffer_queue_pop(pBufferQ, &pRCBuffer);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): qcap2_rcbuffer_queue_pop() failed, qres=%d", __FUNCTION__, __LINE__, qres);
						break;
					}
					std::shared_ptr<qcap2_rcbuffer_t> ZZ_GUARD_NAME(pRCBuffer, qcap2_rcbuffer_release);
					std::shared_ptr<qcap2_av_frame_t> pAVFrame(
						(qcap2_av_frame_t*)qcap2_rcbuffer_lock_data(pRCBuffer),
						[pRCBuffer](qcap2_av_frame_t*) {
							qcap2_rcbuffer_unlock_data(pRCBuffer);
						});

					qcap2_av_frame_set_pts(pAVFrame.get(), now);

					qres = qcap2_rcbuffer_queue_push(pAsrcQ, pRCBuffer);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): qcap2_rcbuffer_queue_push() failed, qres=%d", __FUNCTION__, __LINE__, qres);
						break;
					}
				}

				return QCAP_RT_OK;
			});
			if(qres != QCAP_RS_SUCCESSFUL) {
				LOGE("%s(%d): __testkit__::AddTimerHandler() failed, qres=%d", __FUNCTION__, __LINE__, qres);
				break;
			}

			pTickCtrl->start(_clk());
			qres = qcap2_timer_next(pTimer, 4000);
			if(qres != QCAP_RS_SUCCESSFUL) {
				LOGE("%s(%d): qcap2_timer_next() failed, qres=%d", __FUNCTION__, __LINE__, qres);
				break;
			}

			*ppAsrcQ = pAsrcQ;
		}

		return qres;
	}

	QRESULT StartAsrc(free_stack_t& _FreeStack_, int nBackendType, qcap2_event_t* pEvent, qcap2_audio_source_t** ppAsrc) {
		QRESULT qres = QCAP_RS_SUCCESSFUL;

		switch(1) { case 1:
			const int nBuffers = 8;
			const ULONG nChannels = 2;
			const ULONG nBitsPerSample = 16;
			const ULONG nSampleFrequency = 48000;

			qcap2_audio_source_t* pAsrc = qcap2_audio_source_new();
			_FreeStack_ += [pAsrc]() {
				qcap2_audio_source_delete(pAsrc);
			};

			qcap2_audio_source_set_backend_type(pAsrc, nBackendType);
			qcap2_audio_source_set_acap_id(pAsrc, 1);
			qcap2_audio_source_set_period_time(pAsrc, 20 * 1000);
			qcap2_audio_source_set_event(pAsrc, pEvent);
			qcap2_audio_source_set_frame_count(pAsrc, nBuffers);

			std::shared_ptr<qcap2_audio_format_t> pAudioFormat(qcap2_audio_format_new(), qcap2_audio_format_delete);
			qcap2_audio_format_set_property(pAudioFormat.get(), nChannels, nBitsPerSample, nSampleFrequency);
			qcap2_audio_source_set_audio_format(pAsrc, pAudioFormat.get());

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

			*ppAsrc = pAsrc;
		}

		return qres;
	}

	QRESULT StartAenc_AAC(free_stack_t& _FreeStack_, qcap2_event_t* pEvent, qcap2_audio_encoder_t** ppAenc) {
		QRESULT qres = QCAP_RS_SUCCESSFUL;

		switch(1) { case 1:
			const ULONG nChannels = 2;
			const ULONG nBitsPerSample = 16;
			const ULONG nSampleFrequency = 48000;

			qcap2_audio_encoder_t* pAenc = qcap2_audio_encoder_new();
			_FreeStack_ += [pAenc]() {
				qcap2_audio_encoder_delete(pAenc);
			};

			{
				std::shared_ptr<qcap2_audio_encoder_property_t> pAencProp(qcap2_audio_encoder_property_new(), qcap2_audio_encoder_property_delete);
				qcap2_audio_encoder_property_set_property(pAencProp.get(),
					QCAP_ENCODER_TYPE_SOFTWARE, QCAP_ENCODER_FORMAT_AAC_ADTS,
					nChannels, nBitsPerSample, nSampleFrequency);
				qcap2_audio_encoder_set_audio_property(pAenc, pAencProp.get());
			}

			qcap2_audio_encoder_set_event(pAenc, pEvent);
			qcap2_audio_encoder_set_frame_count(pAenc, 4);
			qcap2_audio_encoder_set_packet_count(pAenc, 16);
			qcap2_audio_encoder_set_multithread(pAenc, true);

			qres = qcap2_audio_encoder_start(pAenc);
			if(qres != QCAP_RS_SUCCESSFUL) {
				LOGE("%s(%d): qcap2_audio_encoder_start() failed, qres=%d", __FUNCTION__, __LINE__, qres);
				break;
			}
			_FreeStack_ += [pAenc]() {
				QRESULT qres;

				qres = qcap2_audio_encoder_stop(pAenc);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_audio_encoder_stop() failed, qres=%d", __FUNCTION__, __LINE__, qres);
				}
			};

			*ppAenc = pAenc;
		}

		return qres;
	}

	QRESULT StartAenc_PCM(free_stack_t& _FreeStack_, qcap2_event_t* pEvent, qcap2_audio_encoder_t** ppAenc) {
		QRESULT qres = QCAP_RS_SUCCESSFUL;

		switch(1) { case 1:
			const ULONG nChannels = 2;
			const ULONG nBitsPerSample = 16;
			const ULONG nSampleFrequency = 48000;

			qcap2_audio_encoder_t* pAenc = qcap2_audio_encoder_new();
			_FreeStack_ += [pAenc]() {
				qcap2_audio_encoder_delete(pAenc);
			};

			{
				std::shared_ptr<qcap2_audio_encoder_property_t> pAencProp(qcap2_audio_encoder_property_new(), qcap2_audio_encoder_property_delete);
				qcap2_audio_encoder_property_set_property(pAencProp.get(),
					QCAP_ENCODER_TYPE_SOFTWARE, QCAP_ENCODER_FORMAT_PCM,
					nChannels, nBitsPerSample, nSampleFrequency);
				qcap2_audio_encoder_set_audio_property(pAenc, pAencProp.get());
			}

			qcap2_audio_encoder_set_event(pAenc, pEvent);
			qcap2_audio_encoder_set_frame_count(pAenc, 4);
			qcap2_audio_encoder_set_packet_count(pAenc, 16);
			qcap2_audio_encoder_set_multithread(pAenc, true);

			qres = qcap2_audio_encoder_start(pAenc);
			if(qres != QCAP_RS_SUCCESSFUL) {
				LOGE("%s(%d): qcap2_audio_encoder_start() failed, qres=%d", __FUNCTION__, __LINE__, qres);
				break;
			}
			_FreeStack_ += [pAenc]() {
				QRESULT qres;

				qres = qcap2_audio_encoder_stop(pAenc);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_audio_encoder_stop() failed, qres=%d", __FUNCTION__, __LINE__, qres);
				}
			};

			*ppAenc = pAenc;
		}

		return qres;
	}
}

using namespace __sc6g0_aenc__;

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

		int64_t nLastTestTime;
		int nTestIter;

		void DoWork() {
			LOGD("%s::%s", typeid(self_t).name(), __FUNCTION__);

			QRESULT qres;

			srand(time(NULL));
			nTestIter = 0;
			nLastTestTime = _clk();

			switch(1) { case 1:
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

			 	int64_t nTestDuration = 5 * 1000000LL;
				wait_for_test_finish([&nTestDuration, this](int ch) -> bool {
					QRESULT qres;
					int64_t now = _clk();
					bool bNextTest;

					bNextTest = (now >= nLastTestTime + nTestDuration);

					switch(ch) {
					case 'n':
					case 'N':
						bNextTest = true;
						break;
					}

					if(bNextTest) switch(1) { case 1:
						nTestIter = nTestIter + 1;
						LOGW("Test: %d (%.2f)", nTestIter, (now - nLastTestTime) / 1000.0);
						nLastTestTime = now;
						nTestDuration = (3000 + rand() % 10000) * 1000LL;

						QRESULT qres_evt = QCAP_RS_SUCCESSFUL;
						qres = ExecInEventHandlers([&qres_evt, this]() -> QRETURN {
							_FreeStack_evt_.flush();
							return OnStart(_FreeStack_evt_, qres_evt);
						});
						if(qres != QCAP_RS_SUCCESSFUL) {
							LOGE("%s(%d): ExecInEventHandlers() failed, qres=%d", __FUNCTION__, __LINE__, qres);
							break;
						}
						if(qres_evt != QCAP_RS_SUCCESSFUL) {
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
			switch(1) { case 1:
				qcap2_event_t* pEvent_asrc;
				qres = NewEvent(_FreeStack_, &pEvent_asrc);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): NewEvent() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				// int nAsrcBackendType = QCAP2_AUDIO_SOURCE_BACKEND_TYPE_NVT_HDAL;
				int nAsrcBackendType = QCAP2_AUDIO_SOURCE_BACKEND_TYPE_TPG;

				qcap2_audio_source_t* pAsrc;
				qres = StartAsrc(_FreeStack_, nAsrcBackendType, pEvent_asrc, &pAsrc);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): StartAsrc() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				qcap2_event_t* pEvent_aenc;
				qres = NewEvent(_FreeStack_, &pEvent_aenc);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): NewEvent() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				qcap2_audio_encoder_t* pAenc;
				qres = StartAenc_AAC(_FreeStack_, pEvent_aenc, &pAenc);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): StartAenc_AAC() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				qcap2_event_t* pEvent_aenc2;
				qres = NewEvent(_FreeStack_, &pEvent_aenc2);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): NewEvent() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				qcap2_audio_encoder_t* pAenc2;
				qres = StartAenc_PCM(_FreeStack_, pEvent_aenc2, &pAenc2);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): StartAenc_PCM() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				qres = AddEventHandler(_FreeStack_, pEvent_asrc,
					std::bind(&self_t::OnAsrc, this, pAsrc, pAenc, pAenc2));
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): AddEventHandler() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				qres = AddEventHandler(_FreeStack_, pEvent_aenc,
					std::bind(&self_t::OnAenc, this, pAenc));
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): AddEventHandler() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				qres = AddEventHandler(_FreeStack_, pEvent_aenc2,
					std::bind(&self_t::OnAenc2, this, pAenc2));
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): AddEventHandler() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
			}

			return QCAP_RT_OK;
		}

		QRETURN OnAsrc(qcap2_audio_source_t* pAsrc, qcap2_audio_encoder_t* pAenc, qcap2_audio_encoder_t* pAenc2) {
			QRESULT qres;
			int64_t now = _clk();

			switch(1) { case 1:
				qcap2_rcbuffer_t* pRCBuffer;
				qres = qcap2_audio_source_pop(pAsrc, &pRCBuffer);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_audio_source_pop() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
				std::shared_ptr<qcap2_rcbuffer_t> ZZ_GUARD_NAME(pRCBuffer, qcap2_rcbuffer_release);

				qres = qcap2_audio_encoder_push(pAenc, pRCBuffer);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_audio_encoder_push() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				qres = qcap2_audio_encoder_push(pAenc2, pRCBuffer);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_audio_encoder_push() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
			}

			return QCAP_RT_OK;
		}

		QRETURN OnAenc(qcap2_audio_encoder_t* pAenc) {
			QRESULT qres;

			switch(1) { case 1:
				qcap2_rcbuffer_t* pRCBuffer;
				qres = qcap2_audio_encoder_pop(pAenc, &pRCBuffer);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_audio_encoder_pop(), qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
				std::shared_ptr<qcap2_rcbuffer_t> ZZ_GUARD_NAME(pRCBuffer, qcap2_rcbuffer_release);
				std::shared_ptr<qcap2_av_packet_t> pAVPacket(
					(qcap2_av_packet_t*)qcap2_rcbuffer_lock_data(pRCBuffer),
					[pRCBuffer](qcap2_av_packet_t*) {
						qcap2_rcbuffer_unlock_data(pRCBuffer);
					});
			}

			return QCAP_RT_OK;
		}

		QRETURN OnAenc2(qcap2_audio_encoder_t* pAenc) {
			QRESULT qres;

			switch(1) { case 1:
				qcap2_rcbuffer_t* pRCBuffer;
				qres = qcap2_audio_encoder_pop(pAenc, &pRCBuffer);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d) qcap2_audio_encoder_pop(), qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}				
#if 1
				std::shared_ptr<qcap2_rcbuffer_t> ZZ_GUARD_NAME(pRCBuffer, qcap2_rcbuffer_release);
#endif

#if 1
				std::shared_ptr<qcap2_av_packet_t> pAVPacket(
					(qcap2_av_packet_t*)qcap2_rcbuffer_lock_data(pRCBuffer),
					[pRCBuffer](qcap2_av_packet_t*) {
						qcap2_rcbuffer_unlock_data(pRCBuffer);
					});
#endif
			}

			return QCAP_RT_OK;
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
