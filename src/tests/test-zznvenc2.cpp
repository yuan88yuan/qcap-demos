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
}

using namespace __test_zznvenc2__;
using __testkit__::wait_for_test_finish;
using __testkit__::TestCase;
using __testkit__::free_stack_t;
using __testkit__::tick_ctrl_t;
using __testkit__::NewEvent;
using __testkit__::AddEventHandler;
using __testkit__::StartVsink_ximage;

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

		qcap2_window_t* pWindow_live;

		void DoWork() {
			LOGD("%s::%s", typeid(self_t).name(), __FUNCTION__);

			QRESULT qres;

			switch(1) { case 1:
				qres = StartEventHandlers();
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): StartEventHandlers() failed, qres=%d", qres);
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
						qres = qcap2_window_handle_events(pWindow_live);
						if(qres != QCAP_RS_SUCCESSFUL) {
							LOGE("%s(%d): qcap2_window_handle_events() failed, qres=%d", __FUNCTION__, __LINE__, qres);
							break;
						}
					}
				}, 1000000LL, 10LL);
			}

			_FreeStack_main_.flush();
		}

		QRETURN OnStart(free_stack_t& _FreeStack_, QRESULT& qres) {
			switch(1) { case 1:
				qres = CreateWindows(_FreeStack_);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): CreateWindows() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				qcap2_video_scaler_t* pVsca;
				qres = StartVsca(_FreeStack_, &pVsca);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): StartVsca() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				qcap2_video_sink_t* pVsink_live;
				qres = StartVsink_ximage(_FreeStack_, QCAP_COLORSPACE_TYPE_I420, 1280, 720, pWindow_live, &pVsink_live);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): StartVsink_ximage() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				qcap2_event_t* pEvent_vsrc;
				qres = NewEvent(_FreeStack_, &pEvent_vsrc);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): NewEvent() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				qcap2_rcbuffer_queue_t* pVsrcQ;
				qres = StartFakeVsrcQ(_FreeStack_, pEvent_vsrc, &pVsrcQ);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): StartFakeVsrcQ() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				qres = AddEventHandler(_FreeStack_, pEvent_vsrc,
					std::bind(&self_t::OnVsrc, this, pVsrcQ, pVsca, pVsink_live));
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): AddEventHandler() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
			}

			return QCAP_RT_OK;
		}

		QRESULT CreateWindows(free_stack_t& _FreeStack_) {
			QRESULT qres = QCAP_RS_SUCCESSFUL;

			switch(1) { case 1:
				pWindow_live = qcap2_window_new();
				_FreeStack_ += [&]() {
					qcap2_window_delete(pWindow_live);
				};

				qcap2_window_set_backend_type(pWindow_live, QCAP2_WINDOW_BACKEND_TYPE_X11);
				qcap2_window_set_rect(pWindow_live, 0, 0, 1280, 720);
				qres = qcap2_window_start(pWindow_live);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_window_start() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
				_FreeStack_ += [&]() {
					QRESULT qres;

					qres = qcap2_window_stop(pWindow_live);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): qcap2_window_stop() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					}
				};
			}

			return qres;
		}

		QRESULT StartVsca(free_stack_t& _FreeStack_, qcap2_video_scaler_t** ppVsca) {
			QRESULT qres = QCAP_RS_SUCCESSFUL;

			switch(1) { case 1:
				const int nBuffers = 4;
				const ULONG nColorSpaceType = QCAP_COLORSPACE_TYPE_I420;
				const ULONG nVideoFrameWidth = 1920;
				const ULONG nVideoFrameHeight = 1080;

				qcap2_video_scaler_t* pVsca = qcap2_video_scaler_new();
				_FreeStack_ += [pVsca]() {
					qcap2_video_scaler_delete(pVsca);
				};

				qcap2_rcbuffer_t** pRCBuffers = new qcap2_rcbuffer_t*[nBuffers];
				_FreeStack_ += [pRCBuffers]() {
					delete[] pRCBuffers;
				};
				for(int i = 0;i < nBuffers;i++) {
					qcap2_rcbuffer_t* pRCBuffer;
					qres = __testkit__::new_video_cudahostbuf(_FreeStack_,
						nColorSpaceType, nVideoFrameWidth, nVideoFrameHeight, cudaHostAllocMapped, &pRCBuffer);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): __testkit__::new_video_cudahostbuf() failed, qres=%d", qres);
						break;
					}

					pRCBuffers[i] = pRCBuffer;
				}

				qcap2_video_scaler_set_backend_type(pVsca, QCAP2_VIDEO_SCALER_BACKEND_TYPE_NPP);
				qcap2_video_scaler_set_multithread(pVsca, false);
				qcap2_video_scaler_set_frame_count(pVsca, nBuffers);
				qcap2_video_scaler_set_buffers(pVsca, &pRCBuffers[0]);
				qcap2_video_scaler_set_src_buffer_hint(pVsca, QCAP2_BUFFER_HINT_CUDAHOST);
				qcap2_video_scaler_set_dst_buffer_hint(pVsca, QCAP2_BUFFER_HINT_CUDAHOST);

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

				*ppVsca = pVsca;
			}

			return qres;
		}

		QRESULT StartFakeVsrcQ(free_stack_t& _FreeStack_, qcap2_event_t* pEvent, qcap2_rcbuffer_queue_t** ppVsrcQ) {
			QRESULT qres = QCAP_RS_SUCCESSFUL;

			switch(1) { case 1:
				const int nBuffers = 4;
				const ULONG nColorSpaceType = QCAP_COLORSPACE_TYPE_NV12;
				const ULONG nVideoFrameWidth = 1920;
				const ULONG nVideoFrameHeight = 1080;

				qcap2_rcbuffer_t** pRCBuffers = new qcap2_rcbuffer_t*[nBuffers];
				_FreeStack_ += [pRCBuffers]() {
					delete[] pRCBuffers;
				};
				for(int i = 0;i < nBuffers;i++) {
					qcap2_rcbuffer_t* pRCBuffer;
					qres = __testkit__::new_video_cudahostbuf(_FreeStack_,
						nColorSpaceType, nVideoFrameWidth, nVideoFrameHeight, cudaHostAllocMapped, &pRCBuffer);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): __testkit__::new_video_sysbuf() failed, qres=%d", qres);
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

				qres = qcap2_cuda_device_synchronize();
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_cuda_device_synchronize() failed, qres=%d", __FUNCTION__, __LINE__,qres);
					break;
				}

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

				qres = AddTimerHandler(_FreeStack_, pTimer,
					std::bind(&self_t::OnFakeVsrc, this, pTimer, pTickCtrl, pBufferQ, pVsrcQ));
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

				*ppVsrcQ = pVsrcQ;
			}

			return qres;
		}

		QRETURN OnFakeVsrc(qcap2_timer_t* pTimer, tick_ctrl_t* pTickCtrl, qcap2_rcbuffer_queue_t* pBufferQ, qcap2_rcbuffer_queue_t* pEventQ) {
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

				qres = qcap2_rcbuffer_queue_push(pEventQ, pRCBuffer);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_rcbuffer_queue_push() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
			}

			return QCAP_RT_OK;
		}

		QRETURN OnVsrc(qcap2_rcbuffer_queue_t* pVsrcQ, qcap2_video_scaler_t* pVsca, qcap2_video_sink_t* pVsink) {
			QRESULT qres;
			int64_t now = _clk();

			switch(1) { case 1:
				qcap2_rcbuffer_t* pRCBuffer_src;
				qres = qcap2_rcbuffer_queue_pop(pVsrcQ, &pRCBuffer_src);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_rcbuffer_queue_pop() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
				std::shared_ptr<qcap2_rcbuffer_t> ZZ_GUARD_NAME(pRCBuffer_src, qcap2_rcbuffer_release);

				qres = qcap2_video_scaler_push(pVsca, &pRCBuffer_src);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_video_scaler_push() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				qcap2_rcbuffer_t* pRCBuffer_sca;
				qres = qcap2_video_scaler_pop(pVsca, &pRCBuffer_sca);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_video_scaler_pop() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
				std::shared_ptr<qcap2_rcbuffer_t> ZZ_GUARD_NAME(pRCBuffer_sca, qcap2_rcbuffer_release);

				qres = qcap2_video_sink_push(pVsink, pRCBuffer_sca);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_video_sink_push() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
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
