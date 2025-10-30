#ifndef __TESTKIT_H__
#define __TESTKIT_H__

#include "qcap2.h"
#include "qcap.linux.h"
#include "qcap2.user.h"

#if BUILD_WITH_NVBUF
#include "qcap2.nvbuf.h"
#endif

#if BUILD_WITH_CUDA
#include "qcap2.cuda.h"
#endif

#include "ZzClock.h"
#include "ZzLog.h"

namespace __testkit__ {
	ZZ_INIT_LOG("testkit");

	using __zz_clock__::_clk;

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

	QRESULT NewEvent(free_stack_t& _FreeStack_, qcap2_event_t** ppEvent) {
		QRESULT qres = QCAP_RS_SUCCESSFUL;

		switch(1) { case 1:
			qcap2_event_t* pEvent = qcap2_event_new();
			_FreeStack_ += [pEvent]() {
				qcap2_event_delete(pEvent);
			};

			qres = qcap2_event_start(pEvent);
			if(qres != QCAP_RS_SUCCESSFUL) {
				LOGE("%s(%d): qcap2_event_start() failed, qres=%d", qres);
				break;
			}
			_FreeStack_ += [pEvent]() {
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

	QRESULT new_video_sysbuf(free_stack_t& _FreeStack_, ULONG nColorSpaceType, ULONG nWidth, ULONG nHeight, qcap2_rcbuffer_t** ppRCBuffer) {
		QRESULT qres = QCAP_RS_SUCCESSFUL;

		switch(1) { case 1:
			qcap2_rcbuffer_t* pRCBuffer = qcap2_rcbuffer_new_av_frame();
			_FreeStack_ += [pRCBuffer]() {
				qcap2_rcbuffer_delete(pRCBuffer);
			};

			qcap2_av_frame_t* pAVFrame = (qcap2_av_frame_t*)qcap2_rcbuffer_get_data(pRCBuffer);
			qcap2_av_frame_set_video_property(pAVFrame, nColorSpaceType, nWidth, nHeight);

			if(! qcap2_av_frame_alloc_buffer(pAVFrame, 32, 1)) {
				qres = QCAP_RS_ERROR_OUT_OF_MEMORY;
				LOGE("%s(%d): qcap2_av_frame_alloc_buffer() failed", __FUNCTION__, __LINE__);
				break;
			}
			_FreeStack_ += [pAVFrame]() {
				qcap2_av_frame_free_buffer(pAVFrame);
			};

			*ppRCBuffer = pRCBuffer;
		}

		return qres;
	}

	QRESULT new_audio_sysbuf(free_stack_t& _FreeStack_, ULONG nChannels, ULONG nSampleFmt, ULONG nSampleFrequency, ULONG nFrameSize, qcap2_rcbuffer_t** ppRCBuffer) {
		QRESULT qres = QCAP_RS_SUCCESSFUL;

		switch(1) { case 1:
			qcap2_rcbuffer_t* pRCBuffer = qcap2_rcbuffer_new_av_frame();
			_FreeStack_ += [pRCBuffer]() {
				qcap2_rcbuffer_delete(pRCBuffer);
			};

			qcap2_av_frame_t* pAVFrame = (qcap2_av_frame_t*)qcap2_rcbuffer_get_data(pRCBuffer);
			qcap2_av_frame_set_audio_property(pAVFrame, nChannels, nSampleFmt, nSampleFrequency, nFrameSize);

			if(! qcap2_av_frame_alloc_buffer(pAVFrame, 32, 1)) {
				qres = QCAP_RS_ERROR_OUT_OF_MEMORY;
				LOGE("%s(%d): qcap2_av_frame_alloc_buffer() failed", __FUNCTION__, __LINE__);
				break;
			}
			_FreeStack_ += [pAVFrame]() {
				qcap2_av_frame_free_buffer(pAVFrame);
			};

			*ppRCBuffer = pRCBuffer;
		}

		return qres;
	}

#if BUILD_WITH_NVBUF
	QRESULT new_video_nvbuf(free_stack_t& _FreeStack_, NvBufSurfaceCreateParams& oNVBufParam, qcap2_rcbuffer_t** ppRCBuffer) {
		QRESULT qres = QCAP_RS_SUCCESSFUL;
		qcap2_rcbuffer_t* pRCBuffer_ret = NULL;

		switch(1) { case 1:
			pRCBuffer_ret = qcap2_rcbuffer_new_av_frame();
			_FreeStack_ += [pRCBuffer_ret]() {
				qcap2_rcbuffer_delete(pRCBuffer_ret);
			};

			qres = qcap2_rcbuffer_alloc_nvbuf(pRCBuffer_ret, &oNVBufParam);
			if(qres != QCAP_RS_SUCCESSFUL) {
				LOGE("%s(%d): qcap2_rcbuffer_alloc_nvbuf() failed, qres=%d", __FUNCTION__, __LINE__, qres);
				break;
			}
			_FreeStack_ += [pRCBuffer_ret]() {
				QRESULT qres;

				qres = qcap2_rcbuffer_free_nvbuf(pRCBuffer_ret);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_rcbuffer_free_nvbuf() failed, qres=%d", __FUNCTION__, __LINE__, qres);
				}
			};

			*ppRCBuffer = pRCBuffer_ret;
		}

		return qres;
	}
#endif // BUILD_WITH_NVBUF

#if BUILD_WITH_CUDA
	QRESULT new_video_cudabuf(free_stack_t& _FreeStack_, ULONG nColorSpaceType, ULONG nWidth, ULONG nHeight, qcap2_rcbuffer_t** ppRCBuffer) {
		QRESULT qres = QCAP_RS_SUCCESSFUL;

		switch(1) { case 1:
			qcap2_rcbuffer_t* pRCBuffer = qcap2_rcbuffer_new_av_frame();
			_FreeStack_ += [pRCBuffer]() {
				qcap2_rcbuffer_delete(pRCBuffer);
			};

			qcap2_av_frame_t* pAVFrame = (qcap2_av_frame_t*)qcap2_rcbuffer_get_data(pRCBuffer);
			qcap2_av_frame_set_video_property(pAVFrame, nColorSpaceType, nWidth, nHeight);

			if(! qcap2_av_frame_alloc_cuda_buffer(pAVFrame, 32, 1)) {
				qres = QCAP_RS_ERROR_OUT_OF_MEMORY;
				LOGE("%s(%d): qcap2_av_frame_alloc_cuda_buffer() failed", __FUNCTION__, __LINE__);
				break;
			}
			_FreeStack_ += [pAVFrame]() {
				qcap2_av_frame_free_cuda_buffer(pAVFrame);
			};

			*ppRCBuffer = pRCBuffer;
		}

		return qres;
	}

	QRESULT new_video_cudahostbuf(free_stack_t& _FreeStack_, ULONG nColorSpaceType, ULONG nWidth, ULONG nHeight, unsigned int nFlags, qcap2_rcbuffer_t** ppRCBuffer) {
		QRESULT qres = QCAP_RS_SUCCESSFUL;

		switch(1) { case 1:
			qcap2_rcbuffer_t* pRCBuffer = qcap2_rcbuffer_new_av_frame();
			_FreeStack_ += [pRCBuffer]() {
				qcap2_rcbuffer_delete(pRCBuffer);
			};

			qcap2_av_frame_t* pAVFrame = (qcap2_av_frame_t*)qcap2_rcbuffer_get_data(pRCBuffer);
			qcap2_av_frame_set_video_property(pAVFrame, nColorSpaceType, nWidth, nHeight);

			if(! qcap2_av_frame_alloc_cuda_host_buffer(pAVFrame, nFlags, 32, 1)) {
				qres = QCAP_RS_ERROR_OUT_OF_MEMORY;
				LOGE("%s(%d): qcap2_av_frame_alloc_cuda_host_buffer() failed", __FUNCTION__, __LINE__);
				break;
			}
			_FreeStack_ += [pAVFrame]() {
				qcap2_av_frame_free_cuda_host_buffer(pAVFrame);
			};

			*ppRCBuffer = pRCBuffer;
		}

		return qres;
	}
#endif // BUILD_WITH_CUDA

	struct TestCase {
		typedef TestCase self_t;

		free_stack_t _FreeStack_main_;
		free_stack_t _FreeStack_evt_;
		qcap2_event_handlers_t* pEventHandlers;

		QRESULT StartEventHandlers() {
			QRESULT qres = QCAP_RS_SUCCESSFUL;

			switch(1) { case 1:
				free_stack_t& _FreeStack_ = _FreeStack_main_;

				pEventHandlers = qcap2_event_handlers_new();
				_FreeStack_ += [&]() {
					qcap2_event_handlers_delete(pEventHandlers);
				};

				qres = qcap2_event_handlers_start(pEventHandlers);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_event_handlers_start() failed, qres=%d", qres);
					break;
				}
				_FreeStack_ += [&]() {
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

				qres = qcap2_event_handlers_invoke(pEventHandlers,
					callback_t::_func, pCallback.get());
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_event_handlers_invoke() failed, qres=%d", __FUNCTION__, __LINE__,qres);
					break;
				}
			}

			return qres;
		}

		QRESULT OnExitEventHandlers() {
			return ExecInEventHandlers([&]() -> QRETURN {
				_FreeStack_evt_.flush();

				return QCAP_RT_OK;
			});
		}

		template<class FUNC>
		QRESULT AddEventHandler(free_stack_t& _FreeStack_, qcap2_event_t* pEvent, FUNC func) {
			QRESULT qres = QCAP_RS_SUCCESSFUL;

			switch(1) { case 1:
				uintptr_t nHandle;
				qres = qcap2_event_get_native_handle(pEvent, &nHandle);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_event_get_native_handle() failed, qres=%d", qres);
					break;
				}

				callback_t* pCallback = new callback_t(func);
				_FreeStack_ += [pCallback]() {
					delete pCallback;
				};

				qres = qcap2_event_handlers_add_handler(pEventHandlers, nHandle,
					callback_t::_func, pCallback);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_event_handlers_add_handler() failed, qres=%d", qres);
					break;
				}
				_FreeStack_ += [&, nHandle]() {
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
		QRESULT AddTimerHandler(free_stack_t& _FreeStack_, qcap2_timer_t* pTimer, FUNC func) {
			QRESULT qres = QCAP_RS_SUCCESSFUL;

			switch(1) { case 1:
				uintptr_t nHandle;
				qres = qcap2_timer_get_native_handle(pTimer, &nHandle);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_timer_get_native_handle() failed, qres=%d", qres);
					break;
				}

				callback_t* pCallback = new callback_t(func);
				_FreeStack_ += [pCallback]() {
					delete pCallback;
				};

				qres = qcap2_event_handlers_add_handler(pEventHandlers, nHandle,
					callback_t::_func, pCallback);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_event_handlers_add_handler() failed, qres=%d", qres);
					break;
				}
				_FreeStack_ += [&, nHandle]() {
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
};

#endif // __TESTKIT_H__