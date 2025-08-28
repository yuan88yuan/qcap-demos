#include "qcap.linux.h"
#include "qcap2.h"
#include "qcap2.user.h"
#include "qcap2.cuda.h"
#include "qcap2.hsb.h"

#include "ZzLog.h"
#include "ZzModules.h"
#include "ZzClock.h"
#include "ZzStats.h"
#include "ZzUtils.h"

#include <fcntl.h>
#include <sys/ioctl.h>
#include <stdlib.h>

#include <memory>
#include <functional>
#include <stack>
#include <cstring>
#include <fstream>
#include <thread>
#include <atomic>
#include <filesystem>

#include <cuda.h>
#include <cuda_runtime_api.h>

ZZ_INIT_LOG("test-hsb-vsrc");

int g_argc = 0;
char** g_argv = NULL;

ZZ_MODULE_DECL(__zz_log__);

using namespace __zz_clock__;

namespace __test_vsrc__ {
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

	template<class FUNC_IDLE>
	void wait_for_test_finish(FUNC_IDLE idle, int64_t dur_num = 1000000LL, int64_t dur_den = 60LL) {
		int err;
		QRESULT qres;

		int fd_stdin = 0; // stdin
		int64_t now = _clk();
		int64_t nTick = now * dur_den / dur_num;

		LOGW("Wait for test...");
		while(true) {
			fd_set readfds;
			FD_ZERO(&readfds);

			int fd_max = -1;
			if(fd_stdin > fd_max) fd_max = fd_stdin;
			FD_SET(fd_stdin, &readfds);

			struct timeval tval;

			int64_t nInterval = (nTick + 1) * dur_num / dur_den - now;
			if(nInterval < 4000) {
				tval.tv_sec  = 0;
				tval.tv_usec = 4000LL;
			} else {
				tval.tv_sec  = nInterval / dur_num;
				tval.tv_usec = nInterval % dur_num;
			}

			err = select(fd_max + 1, &readfds, NULL, NULL, &tval);
			if (err < 0) {
				LOGE("%s(%d): select() failed! err = %d", __FUNCTION__, __LINE__, err);
				break;
			}

			now = _clk();
			nTick++;

			if (FD_ISSET(fd_stdin, &readfds)) {
				int ch = getchar();

				if(ch == 'q')
					break;

				if(! idle(ch))
					break;
			}

			if(err == 0) {
				if(! idle(0))
					break;
			}
		}
		LOGW("Test done.");
	}

	struct free_stack_t : protected std::stack<std::function<void ()> > {
		typedef free_stack_t self_t;
		typedef std::stack<std::function<void ()> > parent_t;

		free_stack_t() {
		}

		~free_stack_t() {
			if(! empty()) {
				LOGE("%s(%d): unexpected value, size()=%d", __FUNCTION__, __LINE__, size());
			}
		}

		template<class FUNC>
		free_stack_t& operator +=(const FUNC& func) {
			push(func);

			return *this;
		}

		void flush() {
			while(! empty()) {
				top()();
				pop();
			}
		}
	};

	struct tick_ctrl_t {
		typedef tick_ctrl_t self_t;

		int num;
		int den;

		explicit tick_ctrl_t() {
		}

		void start(int64_t t) {
			nDenSecs = den * 1000000LL;
			nTimer = t;
		}

		int64_t advance(int64_t t) {
			int64_t nDiff = t - nTimer;
			int64_t nTicks = nDiff * num / nDenSecs;
			int64_t nNextTimer = nTimer + (nTicks + 1) * nDenSecs / num;

			nTimer += nTicks * nDenSecs / num;

			return nNextTimer - t;
		}

		int64_t nDenSecs;
		int64_t nTimer;
	};

	struct callback_t {
		typedef callback_t self_t;
		typedef std::function<QRETURN ()> cb_func_t;

		cb_func_t func;

		template<class FUNC>
		callback_t(FUNC func) : func(func) {
		}

		static QRETURN _func(PVOID pUserData) {
			self_t* pThis = (self_t*)pUserData;

			return pThis->func();
		}
	};

	inline void spinlock_lock(std::atomic_flag& lock) {
		while (lock.test_and_set(std::memory_order_acquire)) // acquire lock
		{
		// Since C++20, it is possible to update atomic_flag's
		// value only when there is a chance to acquire the lock.
		// See also: https://stackoverflow.com/questions/62318642
#if defined(__cpp_lib_atomic_flag_test)
			while (lock.test(std::memory_order_relaxed)) // test lock
#endif
			; // spin
		}
	}

	inline void spinlock_unlock(std::atomic_flag& lock) {
		lock.clear(std::memory_order_release);
	}

	QRESULT new_video_sysbuf(free_stack_t& oFreeStack, ULONG nColorSpaceType, ULONG nWidth, ULONG nHeight, qcap2_rcbuffer_t** ppRCBuffer) {
		QRESULT qres = QCAP_RS_SUCCESSFUL;
		qcap2_rcbuffer_t* pRCBuffer_ret = NULL;

		switch(1) { case 1:
			pRCBuffer_ret = qcap2_rcbuffer_new_av_frame();
			oFreeStack += [pRCBuffer_ret]() {
				qcap2_rcbuffer_delete(pRCBuffer_ret);
			};

			qcap2_av_frame_t* pAVFrame = (qcap2_av_frame_t*)qcap2_rcbuffer_get_data(pRCBuffer_ret);
			qcap2_av_frame_set_video_property(pAVFrame, nColorSpaceType, nWidth, nHeight);

			if(! qcap2_av_frame_alloc_buffer(pAVFrame, 32, 1)) {
				qres = QCAP_RS_ERROR_OUT_OF_MEMORY;
				LOGE("%s(%d): qcap2_av_frame_alloc_buffer() failed", __FUNCTION__, __LINE__);
				break;
			}
			oFreeStack += [pAVFrame]() {
				qcap2_av_frame_free_buffer(pAVFrame);
			};

			*ppRCBuffer = pRCBuffer_ret;
		}

		return qres;
	}

	QRESULT new_video_cuda_hostbuf(free_stack_t& oFreeStack, ULONG nColorSpaceType, ULONG nWidth, ULONG nHeight, qcap2_rcbuffer_t** ppRCBuffer) {
		QRESULT qres = QCAP_RS_SUCCESSFUL;
		qcap2_rcbuffer_t* pRCBuffer_ret = NULL;

		switch(1) { case 1:
			pRCBuffer_ret = qcap2_rcbuffer_new_av_frame();
			oFreeStack += [pRCBuffer_ret]() {
				qcap2_rcbuffer_delete(pRCBuffer_ret);
			};

			qcap2_av_frame_t* pAVFrame = (qcap2_av_frame_t*)qcap2_rcbuffer_get_data(pRCBuffer_ret);
			qcap2_av_frame_set_video_property(pAVFrame, nColorSpaceType, nWidth, nHeight);

			if(! qcap2_av_frame_alloc_cuda_host_buffer(pAVFrame, cudaHostAllocMapped, 32, 1)) {
				qres = QCAP_RS_ERROR_OUT_OF_MEMORY;
				LOGE("%s(%d): qcap2_av_frame_alloc_cuda_host_buffer() failed", __FUNCTION__, __LINE__);
				break;
			}
			oFreeStack += [pAVFrame]() {
				qcap2_av_frame_free_cuda_host_buffer(pAVFrame);
			};

			*ppRCBuffer = pRCBuffer_ret;
		}

		return qres;
	}

	QRESULT NewEvent(qcap2_event_t** ppEvent, free_stack_t& oFreeStack) {
		QRESULT qres = QCAP_RS_SUCCESSFUL;

		switch(1) { case 1:
			qcap2_event_t* pEvent = qcap2_event_new();
			oFreeStack += [pEvent]() {
				qcap2_event_delete(pEvent);
			};

			qres = qcap2_event_start(pEvent);
			if(qres != QCAP_RS_SUCCESSFUL) {
				LOGE("%s(%d): qcap2_event_start() failed, qres=%d", qres);
				break;
			}
			oFreeStack += [pEvent]() {
				QRESULT qres;

				qres = qcap2_event_stop(pEvent);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_event_stop() failed, qres=%d", qres);
				}
			};

			*ppEvent = pEvent;
		}

		return qres;
	}
}

using namespace __test_vsrc__;

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

	struct TestCase {
		typedef TestCase self_t;

		free_stack_t oFreeStack;
		free_stack_t oFreeStack_evt;
		qcap2_event_handlers_t* pEventHandlers;

		QRESULT StartEventHandlers() {
			QRESULT qres = QCAP_RS_SUCCESSFUL;

			switch(1) { case 1:
				pEventHandlers = qcap2_event_handlers_new();
				oFreeStack += [&]() {
					qcap2_event_handlers_delete(pEventHandlers);
				};

				qres = qcap2_event_handlers_start(pEventHandlers);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_event_handlers_start() failed, qres=%d", qres);
					break;
				}
				oFreeStack += [&]() {
					QRESULT qres;

					qres = qcap2_event_handlers_stop(pEventHandlers);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): qcap2_event_handlers_stop() failed, qres=%d", qres);
					}
				};
			}

			return qres;
		}

		template<class FUNC>
		QRESULT ExecInEventHandlers(FUNC func) {
			QRESULT qres = QCAP_RS_SUCCESSFUL;

			switch(1) { case 1:
				std::shared_ptr<callback_t> pCallback(new callback_t(func));

				LOGW("+qcap2_event_handlers_invoke()");
				qres = qcap2_event_handlers_invoke(pEventHandlers,
					callback_t::_func, pCallback.get());
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_event_handlers_invoke() failed, qres=%d", __FUNCTION__, __LINE__,qres);
					break;
				}
				LOGW("-qcap2_event_handlers_invoke()");
			}

			return qres;
		}

		QRESULT OnExitEventHandlers() {
			return ExecInEventHandlers([&]() -> QRETURN {
				LOGW("+oFreeStack_evt.flush()");
				oFreeStack_evt.flush();
				LOGW("-oFreeStack_evt.flush()");

				return QCAP_RT_OK;
			});
		}

		template<class FUNC>
		QRESULT AddEventHandler(qcap2_event_t* pEvent, FUNC func, free_stack_t& oFreeStack) {
			QRESULT qres = QCAP_RS_SUCCESSFUL;

			switch(1) { case 1:
				uintptr_t nHandle;
				qres = qcap2_event_get_native_handle(pEvent, &nHandle);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_event_get_native_handle() failed, qres=%d", qres);
					break;
				}

				callback_t* pCallback = new callback_t(func);
				oFreeStack += [pCallback]() {
					delete pCallback;
				};

				qres = qcap2_event_handlers_add_handler(pEventHandlers, nHandle,
					callback_t::_func, pCallback);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_event_handlers_add_handler() failed, qres=%d", qres);
					break;
				}
				oFreeStack += [&, nHandle]() {
					QRESULT qres;

					qres = qcap2_event_handlers_remove_handler(pEventHandlers, nHandle);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): qcap2_event_handlers_remove_handler() failed, qres=%d", qres);
					}
				};
			}

			return qres;
		}

		template<class FUNC>
		QRESULT AddTimerHandler(qcap2_timer_t* pTimer, FUNC func, free_stack_t& oFreeStack) {
			QRESULT qres = QCAP_RS_SUCCESSFUL;

			switch(1) { case 1:
				uintptr_t nHandle;
				qres = qcap2_timer_get_native_handle(pTimer, &nHandle);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_timer_get_native_handle() failed, qres=%d", qres);
					break;
				}

				callback_t* pCallback = new callback_t(func);
				oFreeStack += [pCallback]() {
					delete pCallback;
				};

				qres = qcap2_event_handlers_add_handler(pEventHandlers, nHandle,
					callback_t::_func, pCallback);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_event_handlers_add_handler() failed, qres=%d", qres);
					break;
				}
				oFreeStack += [&, nHandle]() {
					QRESULT qres;

					qres = qcap2_event_handlers_remove_handler(pEventHandlers, nHandle);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): qcap2_event_handlers_remove_handler() failed, qres=%d", qres);
					}
				};
			}

			return qres;
		}
	};

	struct TestCase1 : public TestCase {
		typedef TestCase1 self_t;
		typedef TestCase super_t;

		ULONG nColorSpaceType;
		ULONG nVideoWidth;
		ULONG nVideoHeight;
		BOOL bVideoIsInterleaved;
		double dVideoFrameRate;

		bool bSnapshot;
		qcap2_rcbuffer_t* pSnapshotBuffer;

		void DoWork() {
			QRESULT qres;

			LOGD("%s::%s", typeid(self_t).name(), __FUNCTION__);

			nColorSpaceType = QCAP_COLORSPACE_TYPE_YUY2;
#if 0
			nVideoWidth = 3840;
			nVideoHeight = 2160;
#else
			nVideoWidth = 1920;
			nVideoHeight = 1080;
#endif
			bVideoIsInterleaved = FALSE;
			dVideoFrameRate = 60;

			switch(1) { case 1:
				qres = StartEventHandlers();
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): StartEventHandlers() failed, qres=%d", qres);
					break;
				}
				std::shared_ptr<void> ZZ_GUARD_NAME(NULL, [&](void*) {
					OnExitEventHandlers();
				});

				QRESULT qres_evt = QCAP_RS_SUCCESSFUL;
				qres = ExecInEventHandlers(std::bind(&self_t::OnStart, this, std::ref(qres_evt)));
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): ExecInEventHandlers() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
				if(qres_evt != QCAP_RS_SUCCESSFUL) {
					break;
				}

				bSnapshot = false;

				wait_for_test_finish([&](int ch) -> bool {
					switch(ch) {
					case 's':
					case 'S':
						bSnapshot = true;
						break;
					}
					return true;
				}, 1000000LL, 10LL);
			}

			oFreeStack.flush();
		}

		QRETURN OnStart(QRESULT& qres) {
			switch(1) { case 1:
				qres = new_video_sysbuf(oFreeStack_evt,
					nColorSpaceType, nVideoWidth, nVideoHeight, &pSnapshotBuffer);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): new_video_sysbuf() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				qcap2_event_t* pVsrcEvent;
				qcap2_video_source_t* pVsrc;
				qres = StartVsrc(&pVsrc, &pVsrcEvent);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): StartVsrc() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				qres = AddEventHandler(pVsrcEvent,
					std::bind(&self_t::OnVsrc, this, pVsrc), oFreeStack_evt);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): AddEventHandler() failed, qres=%d", qres);
					break;
				}
			}

			return QCAP_RT_OK;
		}

		QRESULT StartVsrc(qcap2_video_source_t** ppVsrc, qcap2_event_t** ppEvent) {
			QRESULT qres = QCAP_RS_SUCCESSFUL;

			switch(1) { case 1:
				qcap2_event_t* pEvent;
				qres = NewEvent(&pEvent, oFreeStack_evt);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): NewEvent() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				qcap2_video_source_t* pVsrc = qcap2_video_source_new();
				oFreeStack_evt += [pVsrc]() {
					qcap2_video_source_delete(pVsrc);
				};

				qcap2_video_source_set_backend_type(pVsrc, QCAP2_VIDEO_SOURCE_BACKEND_TYPE_HSB);
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

				std::string ibv_name;
				try {
					std::vector<std::string> devices;
					for (auto const& dir_entry : std::filesystem::directory_iterator {
						std::filesystem::path { "/sys/class/infiniband" } }) {
						devices.push_back(dir_entry.path().filename());
					}
					if (!devices.empty()) {
						std::sort(devices.begin(), devices.end());
						ibv_name = devices[0];
					}
				} catch (...) {
				}

				qcap2_video_source_set_frame_count(pVsrc, 2);
				qcap2_video_source_set_device_ordinal(pVsrc, 0);
				qcap2_video_source_set_hololink_ip(pVsrc, "192.168.0.2");
				qcap2_video_source_set_ibv_name(pVsrc, ibv_name.c_str());
				qcap2_video_source_set_ibv_port(pVsrc, 1);

				qres = qcap2_video_source_start(pVsrc);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_video_source_start() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
				oFreeStack_evt += [pVsrc]() {
					QRESULT qres;

					qres = qcap2_video_source_stop(pVsrc);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): qcap2_video_source_stop() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					}
				};

				*ppVsrc = pVsrc;
				*ppEvent = pEvent;
			}

			return qres;
		}

		QRETURN OnVsrc(qcap2_video_source_t* pVsrc) {
			QRESULT qres;
			cudaError_t cuerr;

			switch(1) { case 1:
				qcap2_rcbuffer_t* pRCBuffer;
				qres = qcap2_video_source_pop(pVsrc, &pRCBuffer);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_video_source_pop() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
				std::shared_ptr<qcap2_rcbuffer_t> pRCBuffer_(pRCBuffer,
					qcap2_rcbuffer_release);
				std::shared_ptr<qcap2_av_frame_t> pAVFrame(
					(qcap2_av_frame_t*)qcap2_rcbuffer_lock_data(pRCBuffer),
					[pRCBuffer](qcap2_av_frame_t*) {
						qcap2_rcbuffer_unlock_data(pRCBuffer);
					});

				if(bSnapshot) {
					bSnapshot = false;

					int64_t nPTS;
					qcap2_av_frame_get_pts(pAVFrame.get(), &nPTS);
					uint8_t* pBuffer;
					int nStride;
					qcap2_av_frame_get_buffer(pAVFrame.get(), &pBuffer, &nStride);

					std::shared_ptr<qcap2_av_frame_t> pAVFrame_dst(
						(qcap2_av_frame_t*)qcap2_rcbuffer_lock_data(pSnapshotBuffer),
						[&](qcap2_av_frame_t*) {
							qcap2_rcbuffer_unlock_data(pSnapshotBuffer);
						});
					uint8_t* pBuffer_dst;
					int nStride_dst;
					qcap2_av_frame_get_buffer(pAVFrame_dst.get(), &pBuffer_dst, &nStride_dst);

					LOGD("snapshot: (%p %d) (%p %d) %dx%d",
						pBuffer_dst, nStride_dst,
						pBuffer, nStride, nVideoWidth, nVideoHeight);

					// FOR YUY2 ONLY
					cuerr = cudaMemcpy2D(pBuffer_dst, nStride_dst,
						pBuffer, nStride, nVideoWidth * 2, nVideoHeight, cudaMemcpyDeviceToHost);
					if(cuerr != cudaSuccess) {
						LOGE("%s(%d): cudaMemcpy2D() failed, cuerr=%d", __FUNCTION__, __LINE__, cudaMemcpy2D);
						break;
					}

					qres = qcap2_save_raw_video_frame(pSnapshotBuffer, "snapshot");
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): qcap2_save_raw_video_frame() failed, qres=%d", __FUNCTION__, __LINE__, qres);
						break;
					}
				}
				// LOGD("%.2f, %p, %d", nPTS / 1000.0, pBuffer, nStride);
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
