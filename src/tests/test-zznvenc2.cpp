#include <stdlib.h>
#include <time.h>

#include "qcap2.h"

#include "ZzLog.h"
#include "ZzModules.h"
#include "ZzClock.h"
#include "ZzStats.h"
#include "ZzUtils.h"
#include "testkit.h"

ZZ_INIT_LOG("test-zznvenc2");

int g_argc = 0;
char** g_argv = NULL;

ZZ_MODULE_DECL(__zz_log__);

using namespace __zz_clock__;
using __testkit__::wait_for_test_finish;
using __testkit__::TestCase;
using __testkit__::free_stack_t;
using __testkit__::tick_ctrl_t;
using __testkit__::NewEvent;

namespace __test_zznvenc2__ {
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

	QRESULT StartFakeVsrcQ(free_stack_t& _FreeStack_, qcap2_event_handlers_t* pEventHandlers, qcap2_event_t** ppEvent, qcap2_rcbuffer_queue_t** ppVsrcQ) {
		QRESULT qres = QCAP_RS_SUCCESSFUL;

		switch(1) { case 1:
			const int nBuffers = 4;
			const ULONG nWidth = 1920;
			const ULONG nHeight = 1080;
			const ULONG nColorSpaceType = QCAP_COLORSPACE_TYPE_NV12;
			const double dFrameRate = 60.0f;

			qcap2_event_t* pEvent;
			qres = NewEvent(_FreeStack_, &pEvent);
			if(qres != QCAP_RS_SUCCESSFUL) {
				LOGE("%s(%d): NewEvent() failed, qres=%d", __FUNCTION__, __LINE__, qres);
				break;
			}

			qcap2_rcbuffer_t** pRCBuffers = new qcap2_rcbuffer_t*[nBuffers];
			_FreeStack_ += [pRCBuffers]() {
				delete[] pRCBuffers;
			};
			for(int i = 0;i < nBuffers;i++) {
				qcap2_rcbuffer_t* pRCBuffer;
				qres = __testkit__::new_video_sysbuf(_FreeStack_, nColorSpaceType, nWidth, nHeight, &pRCBuffer);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): __testkit__::new_video_sysbuf() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				qres = qcap2_load_picture(pRCBuffer, "vsrc.jpg");
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_load_picture() failed, qres=%d", __FUNCTION__, __LINE__,qres);
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

			qcap2_rcbuffer_queue_t* pVsrcQ = qcap2_rcbuffer_queue_new();
			_FreeStack_ += [pVsrcQ]() {
				qcap2_rcbuffer_queue_delete(pVsrcQ);
			};

			qcap2_rcbuffer_queue_set_max_buffers(pVsrcQ, nBuffers);
			qcap2_rcbuffer_queue_set_event(pVsrcQ, pEvent);

			qres = qcap2_rcbuffer_queue_start(pVsrcQ);
			if(qres != QCAP_RS_SUCCESSFUL) {
				LOGE("%s(%d): qcap2_rcbuffer_queue_start() failed, qres=%d", __FUNCTION__, __LINE__, qres);
				break;
			}
			_FreeStack_ += [pVsrcQ]() {
				QRESULT qres;

				qres = qcap2_rcbuffer_queue_stop(pVsrcQ);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_rcbuffer_queue_stop() failed, qres=%d", __FUNCTION__, __LINE__, qres);
				}
			};

			tick_ctrl_t* pTickCtrl = new tick_ctrl_t();
			_FreeStack_ += [pTickCtrl]() {
				delete pTickCtrl;
			};

			qcap2_rational_t oFrameRate =  qcap2_d2q(dFrameRate, INT_MAX);
			pTickCtrl->num = oFrameRate.num;
			pTickCtrl->den = oFrameRate.den;

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

			qres = __testkit__::AddTimerHandler(_FreeStack_, pEventHandlers, pTimer,
				[pTimer, pTickCtrl, pBufferQ, pVsrcQ]() -> QRETURN {

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

					qres = qcap2_rcbuffer_queue_push(pVsrcQ, pRCBuffer);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): qcap2_rcbuffer_queue_push() failed, qres=%d", __FUNCTION__, __LINE__, qres);
						break;
					}
				}

				return QCAP_RT_OK;
			});
			if(qres != QCAP_RS_SUCCESSFUL) {
				LOGE("%s(%d): AddTimerHandler() failed, qres=%d", __FUNCTION__, __LINE__, qres);
				break;
			}

			pTickCtrl->start(_clk());
			qres = qcap2_timer_next(pTimer, 4000);
			if(qres != QCAP_RS_SUCCESSFUL) {
				LOGE("%s(%d): qcap2_timer_next() failed, qres=%d", __FUNCTION__, __LINE__, qres);
				break;
			}

			*ppEvent = pEvent;
			*ppVsrcQ = pVsrcQ;
		}

		return qres;
	}
}

using namespace __test_zznvenc2__;

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

		void DoWork() {
			LOGD("%s::%s", typeid(self_t).name(), __FUNCTION__);

			QRESULT qres;

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

				wait_for_test_finish([&](int ch) -> bool {
					return true;
				}, 1000000LL, 10LL);
			}

			_FreeStack_main_.flush();
		}

		QRETURN OnStart(free_stack_t& _FreeStack_, QRESULT& qres) {
			switch(1) { case 1:
				qcap2_event_t* pEvent_vsrc;
				qcap2_rcbuffer_queue_t* pVsrcQ;
				qres = StartFakeVsrcQ(_FreeStack_, pEventHandlers, &pEvent_vsrc, &pVsrcQ);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): StartFakeVsrcQ() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				qres = StartShareRecord(_FreeStack_);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): StartFakeVsrcQ() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				qres = AddEventHandler(_FreeStack_, pEvent_vsrc,
					std::bind(&self_t::OnVsrcQ, this, pVsrcQ));
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): AddEventHandler() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
			}

			return QCAP_RT_OK;
		}

		QRESULT StartShareRecord(free_stack_t& _FreeStack_) {
			QRESULT qres = QCAP_RS_SUCCESSFUL;

			switch(1) { case 1:
				const ULONG nWidth = 1920;
				const ULONG nHeight = 1080;
				const ULONG nColorSpaceType = QCAP_COLORSPACE_TYPE_NV12;
				const double dFrameRate = 60.0f;

				qres = QCAP_SET_VIDEO_SHARE_RECORD_PROPERTY_EX(0, 0,
					QCAP_ENCODER_TYPE_ZZNVCODEC2,
					QCAP_ENCODER_FORMAT_H264,
					nColorSpaceType,
					nWidth,
					nHeight,
					dFrameRate,
					QCAP_RECORD_PROFILE_HIGH,
					QCAP_RECORD_LEVEL_51,
					QCAP_RECORD_ENTROPY_CAVLC,
					QCAP_RECORD_COMPLEXITY_0,
					QCAP_RECORD_MODE_CBR,
					8000,
					12 * 1000000,
					60, 0, FALSE,
					0, 0, 0, FALSE, FALSE, FALSE, 0, 0, 0, 0, 0, 0, 0,
					NULL, FALSE, FALSE);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): QCAP_SET_VIDEO_SHARE_RECORD_PROPERTY_EX() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				qres = QCAP_START_SHARE_RECORD(0, NULL, QCAP_RECORD_FLAG_ENCODE | QCAP_RECORD_FLAG_VIDEO_ONLY);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): QCAP_START_SHARE_RECORD() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
				_FreeStack_ += []() {
					QRESULT qres;

					qres = QCAP_STOP_SHARE_RECORD(0);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): QCAP_STOP_SHARE_RECORD() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					}
				};
			}

			return qres;
		}

		QRETURN OnVsrcQ(qcap2_rcbuffer_queue_t* pVsrcQ) {
			QRESULT qres;
			int64_t now = _clk();

			switch(1) { case 1:
				const ULONG nWidth = 1920;
				const ULONG nHeight = 1080;
				const ULONG nColorSpaceType = QCAP_COLORSPACE_TYPE_NV12;

				qcap2_rcbuffer_t* pRCBuffer;
				qres = qcap2_rcbuffer_queue_pop(pVsrcQ, &pRCBuffer);
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

				int64_t pts;
				qcap2_av_frame_get_pts(pAVFrame.get(), &pts);

#if 1
				{
					static int64_t last_pts = 0;
					LOG_ENTER("vscrQ %.2f(%.2f)", pts / 1000.0, (pts - last_pts) / 1000.0);
					last_pts = pts;
				}
#endif

#if 1
				BYTE* pFrameBuffer;
				ULONG nFrameBufferLen;
				qcap2_rcbuffer_to_buffer(pRCBuffer, &pFrameBuffer, &nFrameBufferLen);

				double dSampleTime = pts / 1000000.0;

				qres = QCAP_SET_VIDEO_SHARE_RECORD_UNCOMPRESSION_BUFFER(0,
					nColorSpaceType, nWidth, nHeight,
					pFrameBuffer, nFrameBufferLen, dSampleTime);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_rcbuffer_queue_pop() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
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
