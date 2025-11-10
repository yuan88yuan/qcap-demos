#include "qcap2.h"
#include "qcap.linux.h"
#include "qcap2.user.h"
#include "qcap2.v4l2.h"

#include "ZzLog.h"
#include "ZzModules.h"
#include "ZzClock.h"
#include "ZzStats.h"
#include "ZzUtils.h"
#include "testkit.h"

ZZ_INIT_LOG("test-deint");

int g_argc = 0;
char** g_argv = NULL;

ZZ_MODULE_DECL(__zz_log__);

using namespace __zz_clock__;
using __testkit__::wait_for_test_finish;
using __testkit__::free_stack_t;
using __testkit__::callback_t;
using __testkit__::tick_ctrl_t;
using __testkit__::TestCase;
using __testkit__::NewEvent;

namespace __test_deint__ {
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
				qres = __testkit__::new_video_sysbuf(_FreeStack_, QCAP_COLORSPACE_TYPE_YUY2, 1920, 1080, &pRCBuffer);
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

			pTickCtrl->num = 30 * 1000LL;
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

using namespace __test_deint__;

struct App0 {
	typedef App0 self_t;

	static int Main() {
		self_t app;
		return app.Run();
	}

	App0() {
	}

	~App0() {
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
				LOGE("%s(%d): select failed! err = %d", __FUNCTION__, __LINE__, err);
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
			QRESULT qres;

			LOGD("%s::%s", typeid(self_t).name(), __FUNCTION__);

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

				qcap2_video_scaler_t* pVsca;
				qres = StartVsca(_FreeStack_, &pVsca);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): StartVsca() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				qres = AddEventHandler(_FreeStack_, pEvent_vsrc,
					std::bind(&self_t::OnVsrcQ, this, pVsrcQ, pVsca));
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): AddEventHandler() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
			}

			return QCAP_RT_OK;
		}

		QRETURN OnVsrcQ(qcap2_rcbuffer_queue_t* pVsrcQ, qcap2_video_scaler_t* pVsca) {
			QRESULT qres;
			int64_t now = _clk();

			switch(1) { case 1:
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

				{
					static int64_t last_pts = 0;
					LOG_ENTER("vsca %.2f(%.2f)", pts / 1000.0, (pts - last_pts) / 1000.0);
					last_pts = pts;
				}

				qres = qcap2_video_scaler_push(pVsca, pRCBuffer);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_video_scaler_push() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
			}

			return QCAP_RT_OK;
		}

		QRESULT StartVsca(free_stack_t& _FreeStack_, qcap2_video_scaler_t** ppVsca) {
			QRESULT qres = QCAP_RS_SUCCESSFUL;

			switch(1) { case 1:
				const int nBuffers = 4;
				const ULONG nColorSpaceType = QCAP_COLORSPACE_TYPE_YUY2;
				const ULONG nVideoFrameWidth = 1920;
				const ULONG nVideoFrameHeight = 1080;

				qcap2_event_t* pEvent;
				qres = NewEvent(_FreeStack_, &pEvent);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): NewEvent() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				qcap2_video_scaler_t* pVsca = qcap2_video_scaler_new();
				_FreeStack_ += [pVsca]() {
					qcap2_video_scaler_delete(pVsca);
				};

				qcap2_video_scaler_set_backend_type(pVsca, QCAP2_VIDEO_SCALER_BACKEND_TYPE_FF_FILTER_GRAPH);
				qcap2_video_scaler_set_multithread(pVsca, false);
				qcap2_video_scaler_set_frame_count(pVsca, nBuffers);
				qcap2_video_scaler_set_event(pVsca, pEvent);
				qcap2_video_scaler_set_filter_graph(pVsca, "yadif=mode=send_frame:parity=tff:deint=all");

				{
					std::shared_ptr<qcap2_video_format_t> pVideoFormat(
						qcap2_video_format_new(), qcap2_video_format_delete);

					qcap2_video_format_set_property(pVideoFormat.get(),
						nColorSpaceType, nVideoFrameWidth, nVideoFrameHeight, FALSE, 60.0);

					qcap2_video_scaler_set_video_format(pVsca, pVideoFormat.get());
				}

				qres = qcap2_video_scaler_start(pVsca);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_video_scaler_start() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
				_FreeStack_ += [pVsca]() {
					QRESULT qres;

					qres = qcap2_video_scaler_stop(pVsca);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): qcap2_video_scaler_stop() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					}
				};

				qres = AddEventHandler(_FreeStack_, pEvent,
					std::bind(&self_t::OnVsca, this, pVsca));
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): AddEventHandler() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				*ppVsca = pVsca;
			}

			return qres;
		}

		QRETURN OnVsca(qcap2_video_scaler_t* pVsca) {
			QRESULT qres;
			int64_t now = _clk();

			switch(1) { case 1:
				qcap2_rcbuffer_t* pRCBuffer;
				qres = qcap2_video_scaler_pop(pVsca, &pRCBuffer);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_video_scaler_pop() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
				std::shared_ptr<qcap2_rcbuffer_t> pRCBuffer_(pRCBuffer, qcap2_rcbuffer_release);

				std::shared_ptr<qcap2_av_frame_t> pAVFrame(
					(qcap2_av_frame_t*)qcap2_rcbuffer_lock_data(pRCBuffer),
					[pRCBuffer](qcap2_av_frame_t*) {
						qcap2_rcbuffer_unlock_data(pRCBuffer);
					});

				int64_t pts;
				qcap2_av_frame_get_pts(pAVFrame.get(), &pts);

				{
					static int64_t last_pts = 0;
					LOG_LEAVE("vsca %.2f(%.2f)", pts / 1000.0, (pts - last_pts) / 1000.0);
					last_pts = pts;
				}

#if 0
				char fn[PATH_MAX];
				sprintf(fn, "testcase1");
				qcap2_save_raw_video_frame(pRCBuffer, fn);
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
	{
		modules_init_t _modules_init_;
		ret = App0::Main();
	}

	return ret;
}
