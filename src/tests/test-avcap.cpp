#include "qcap2.h"
#include "qcap.linux.h"
#include "qcap2.v4l2.h"
#include "qcap2.v4l2.ioctl.h"
#include "qcap2.user.h"

#if BUILD_WITH_NVBUF
#include "qcap2.nvbuf.h"
#endif // BUILD_WITH_NVBUF

#include "qcap2.cuda.h"

#include "ZzLog.h"
#include "ZzModules.h"
#include "ZzClock.h"
#include "ZzStats.h"
#include "ZzUtils.h"

#include <sched.h>
#include <stdlib.h>
#include <string>
#include <memory>
#include <functional>
#include <stack>
#include <fstream>

ZZ_INIT_LOG("test-avcap");

int g_argc = 0;
char** g_argv = NULL;

ZZ_MODULE_DECL(__zz_log__);

using namespace __zz_clock__;

namespace __test_avcap__ {
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
				LOG_ENTER("qcap2_rcbuffer_delete(pRCBuffer) %p %d %d",
					pRCBuffer, qcap2_rcbuffer_use_count(pRCBuffer), qcap2_rcbuffer_weak_count(pRCBuffer));
				qcap2_rcbuffer_delete(pRCBuffer);
				LOG_LEAVE("qcap2_rcbuffer_delete(pRCBuffer)");
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

	QRESULT ConfigMDIN() {
		int err;
		QRESULT qres = QCAP_RS_SUCCESSFUL;

		switch(1) { case 1:
			int nFd = open("/dev/video1", O_RDWR | O_NONBLOCK, 0);
			if(nFd == -1) {
				err = errno;
				LOGE("%s(%d): open() failed, err=%d", __FUNCTION__, __LINE__, err);
				qres = QCAP_RS_ERROR_INVALID_DEVICE;
				break;
			}
			ZzUtils::Scoped ZZ_GUARD_NAME(
				[nFd]() {
					close(nFd);
				}
			);

			qcap2_v4l2_ctrl_mdin_panel_t ctrl;

			ctrl.ctrl_type = qcap2_v4l2_ctrl_mdin_ctrl_type_panel;
			ctrl.VideoOutMode = 0;//pip:1 full:0
			ctrl.SelPort = 3;//shown on otput ch1
			err = qcap2_video_sink_set_panel(nFd, &ctrl);
			if(err < 0) {
				err = errno;
				LOGE("%s(%d): qcap2_video_sink_set_panel() failed, err=%d", __FUNCTION__, __LINE__, err);
				qres = QCAP_RS_ERROR_GENERAL;
				break;
			}
		}

		return qres;
	}
}
using namespace __test_avcap__;

extern cudaError_t zppiHoriDownscale2X_Y210(uchar1* pSrc, int nSrcStep, uchar1* pDst, int nDstStep, int nWidth, int nHeight);

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

				LOG_ENTER("qcap2_event_handlers_invoke()");
				qres = qcap2_event_handlers_invoke(pEventHandlers,
					callback_t::_func, pCallback.get());
				LOG_LEAVE("qcap2_event_handlers_invoke()");
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_event_handlers_invoke() failed, qres=%d", __FUNCTION__, __LINE__,qres);
					break;
				}
			}

			return qres;
		}

		QRESULT OnExitEventHandlers() {
			return ExecInEventHandlers([&]() -> QRETURN {
				LOG_ENTER("_FreeStack_evt_.flush()");
				_FreeStack_evt_.flush();
				LOG_LEAVE("_FreeStack_evt_.flush()");

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

	struct TestCase1 : public TestCase {
		typedef TestCase1 self_t;
		typedef TestCase super_t;

		free_stack_t _FreeStack_avsrc_;

		ULONG nMaxVideoFrameWidth;
		ULONG nMaxVideoFrameHeight;
		qcap2_block_lock_t* pVsrcLock;
		qcap2_block_lock_t* pAsrcLock;
		qcap2_rcbuffer_queue_t* pVsrcQ;
		qcap2_rcbuffer_queue_t* pAsrcQ;
		int64_t nVideoFrame;
		int64_t nAudioFrame;

		ULONG nVideoWidth;
		ULONG nVideoHeight;
		BOOL bVideoIsInterleaved;
		double dVideoFrameRate;
		ULONG nAudioChannels;
		ULONG nAudioBitsPerSample;
		ULONG nAudioSampleFrequency;

		void DoWork() {
			LOGD("%s::%s", typeid(self_t).name(), __FUNCTION__);

			QRESULT qres;

			nMaxVideoFrameWidth = 4096;
			nMaxVideoFrameHeight = 2160;

			switch(1) { case 1:
				qres = StartEventHandlers();
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): StartEventHandlers() failed, qres=%d", qres);
					break;
				}
				ZzUtils::Scoped ZZ_GUARD_NAME(
					[&]() {
						OnExitEventHandlers();
					}
				);

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
					switch(ch) {
					case '1':
						qcap2_debug_set(0, 1);
						break;

					case '2':
						qcap2_debug_set(1, 1);
						break;
					}
					return true;
				}, 1000000LL, 10LL);
			}

			_FreeStack_main_.flush();
		}

		QRETURN OnStart(free_stack_t& _FreeStack_, QRESULT& qres) {
			switch(1) { case 1:
				qres = ConfigMDIN();
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): ConfigMDIN() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				PVOID pDevice;
				qres = StartDevice(_FreeStack_, &pDevice);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): CreateDevice() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				_FreeStack_ += [&]() {
					LOG_ENTER("_FreeStack_avsrc_.flush()");
					_FreeStack_avsrc_.flush();
					LOG_LEAVE("_FreeStack_avsrc_.flush()");
				};
			}

			return QCAP_RT_OK;
		}

		QRESULT StartDevice(free_stack_t& _FreeStack_, PVOID* ppDevice) {
			QRESULT qres = QCAP_RS_SUCCESSFUL;

			switch(1) { case 1:
				const ULONG nColorSpaceType = QCAP_COLORSPACE_TYPE_Y210;

				PVOID pDevice;
				qres = QCAP_CREATE( "SC0750 PCI", 0, NULL, &pDevice, TRUE, TRUE );
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): QCAP_CREATE() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
				_FreeStack_ += [pDevice]() {
					QRESULT qres;

					LOG_ENTER("QCAP_DESTROY(pDevice)");
					qres = QCAP_DESTROY(pDevice);
					LOG_LEAVE("QCAP_DESTROY(pDevice)");
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): QCAP_DESTROY() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					}
				};

				QCAP_REGISTER_SIGNAL_REMOVED_CALLBACK(pDevice, _OnSignalRemoved, this);
				QCAP_REGISTER_FORMAT_CHANGED_CALLBACK(pDevice, _OnFormatChanged, this);
				QCAP_REGISTER_VIDEO_PREVIEW_CALLBACK(pDevice, _OnVideoPreview, this);
				QCAP_REGISTER_AUDIO_PREVIEW_CALLBACK(pDevice, _OnAudioPreview, this);
				QCAP_SET_VIDEO_DEFAULT_OUTPUT_FORMAT(pDevice, nColorSpaceType, 0, 0, 0, 0.0);
				QCAP_SET_VIDEO_INPUT(pDevice, QCAP_INPUT_TYPE_SDI);
				QCAP_SET_AUDIO_INPUT(pDevice, QCAP_INPUT_TYPE_EMBEDDED_AUDIO);
				QCAP_SET_DEVICE_CUSTOM_PROPERTY(pDevice, QCAP_DEVPROP_IO_METHOD, 1);
				QCAP_SET_DEVICE_CUSTOM_PROPERTY(pDevice, QCAP_DEVPROP_FORCE_USERPTR, 1);
				QCAP_SET_DEVICE_CUSTOM_PROPERTY(pDevice, QCAP_DEVPROP_AUDIO_CHANNEL, 8);

				ULONG nMaxVideoBufferLength = nMaxVideoFrameWidth * nMaxVideoFrameHeight * 2 * sizeof(uint16_t);
				for(int i = 0;i < 4;i++) {
					BYTE* pGPUDirectbuffer;
					qres = QCAP_ALLOC_VIDEO_GPUDIRECT_PREVIEW_BUFFER_EX(pDevice, &pGPUDirectbuffer,
						nMaxVideoBufferLength, QCAP_CUDAHOST_ALLOC_MAPPED);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): QCAP_ALLOC_VIDEO_GPUDIRECT_PREVIEW_BUFFER_EX() failed, qres=%d", __FUNCTION__, __LINE__, qres);
						break;
					}
					_FreeStack_ += [pDevice, pGPUDirectbuffer, nMaxVideoBufferLength]() {
						QRESULT qres;

						qres = QCAP_FREE_VIDEO_GPUDIRECT_PREVIEW_BUFFER(pDevice, pGPUDirectbuffer, nMaxVideoBufferLength);
						if(qres != QCAP_RS_SUCCESSFUL) {
							LOGE("%s(%d): QCAP_FREE_VIDEO_GPUDIRECT_PREVIEW_BUFFER() failed, qres=%d", __FUNCTION__, __LINE__, qres);
						}
					};

					qres = QCAP_BIND_VIDEO_GPUDIRECT_PREVIEW_BUFFER(pDevice, i, pGPUDirectbuffer, nMaxVideoBufferLength);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): QCAP_BIND_VIDEO_GPUDIRECT_PREVIEW_BUFFER() failed, qres=%d", __FUNCTION__, __LINE__, qres);
						break;
					}
					_FreeStack_ += [pDevice, i, pGPUDirectbuffer, nMaxVideoBufferLength]() {
						QRESULT qres;

						qres = QCAP_UNBIND_VIDEO_GPUDIRECT_PREVIEW_BUFFER(pDevice, i, pGPUDirectbuffer, nMaxVideoBufferLength);
						if(qres != QCAP_RS_SUCCESSFUL) {
							LOGE("%s(%d): QCAP_UNBIND_VIDEO_GPUDIRECT_PREVIEW_BUFFER() failed, qres=%d", __FUNCTION__, __LINE__, qres);
						}
					};
				}
				if(qres != QCAP_RS_SUCCESSFUL)
					break;

				pVsrcLock = qcap2_block_lock_new();
				_FreeStack_ += [&]() {
					qcap2_block_lock_delete(pVsrcLock);
				};

				pAsrcLock = qcap2_block_lock_new();
				_FreeStack_ += [&]() {
					qcap2_block_lock_delete(pAsrcLock);
				};

				nVideoFrame = 0;
				nAudioFrame = 0;

				qres = QCAP_RUN(pDevice);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): QCAP_RUN() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
				_FreeStack_ += [pDevice]() {
					QRESULT qres;

					LOG_ENTER("QCAP_STOP(pDevice)");
					qres = QCAP_STOP(pDevice);
					LOG_LEAVE("QCAP_STOP(pDevice)");
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): QCAP_STOP() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					}
				};

				*ppDevice = pDevice;
			}

			return qres;
		}

		QRESULT StartAVSrc(free_stack_t& _FreeStack_) {
			QRESULT qres = QCAP_RS_SUCCESSFUL;

			switch(1) { case 1:
				qcap2_rcbuffer_queue_t* pVscaQ;
				qres = StartVscaQ(_FreeStack_, &pVscaQ);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): StartVscaQ() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				qcap2_video_sink_t* pVsink;
				qres = StartVsink(_FreeStack_, &pVsink);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): StartVsink() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				qres = StartVsrcQ(_FreeStack_, pVscaQ, pVsink);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): StartVsrcQ() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				qcap2_audio_sink_t* pAsink;
				qres = StartAsink(_FreeStack_, &pAsink);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): StartAsink() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				qres = StartAsrcQ(_FreeStack_, pAsink);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): StartAsrcQ() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				qcap2_block_lock_grant(pVsrcLock, true);
				qcap2_block_lock_grant(pAsrcLock, true);
				_FreeStack_ += [&]() {
					LOG_ENTER("pAsrcLock, pVsrcLock false");
					qcap2_block_lock_grant(pAsrcLock, false);
					qcap2_block_lock_grant(pVsrcLock, false);
					LOG_LEAVE("pAsrcLock, pVsrcLock false");
				};
			}

			return qres;
		}

		QRESULT StartVscaQ(free_stack_t& _FreeStack_, qcap2_rcbuffer_queue_t** ppVscaQ) {
			QRESULT qres = QCAP_RS_SUCCESSFUL;

			switch(1) { case 1:
				const int nBuffers = 8;
				const ULONG nColorSpaceType = QCAP_COLORSPACE_TYPE_Y210;
				const ULONG nVideoFrameWidth = 1920;
				const ULONG nVideoFrameHeight = 1080 / 2;

				qcap2_rcbuffer_queue_t* pVscaQ = qcap2_rcbuffer_queue_new();
				_FreeStack_ += [pVscaQ]() {
					LOG_ENTER("qcap2_rcbuffer_queue_delete(pVscaQ)");
					qcap2_rcbuffer_queue_delete(pVscaQ);
					LOG_LEAVE("qcap2_rcbuffer_queue_delete(pVscaQ)");
				};

				qcap2_rcbuffer_t** pRCBuffers = new qcap2_rcbuffer_t*[nBuffers];
				_FreeStack_ += [pRCBuffers]() {
					delete[] pRCBuffers;
				};
				for(int i = 0;i < nBuffers;i++) {
					qres = new_video_cudahostbuf(_FreeStack_, nColorSpaceType,
						nVideoFrameWidth, nVideoFrameHeight, cudaHostAllocMapped, &pRCBuffers[i]);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): new_video_cudahostbuf() failed, qres=%d", __FUNCTION__, __LINE__,qres);
						break;
					}
				}
				if(qres != QCAP_RS_SUCCESSFUL)
					break;

				qcap2_rcbuffer_queue_set_buffers(pVscaQ, pRCBuffers);
				qcap2_rcbuffer_queue_set_max_buffers(pVscaQ, nBuffers);

				qres = qcap2_rcbuffer_queue_start(pVscaQ);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_rcbuffer_queue_start() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
				_FreeStack_ += [pVscaQ]() {
					QRESULT qres;

					LOG_ENTER("qcap2_rcbuffer_queue_stop(pVscaQ)");
					qres = qcap2_rcbuffer_queue_stop(pVscaQ);
					LOG_LEAVE("qcap2_rcbuffer_queue_stop(pVscaQ)");
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): qcap2_rcbuffer_queue_stop() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					}
				};

				*ppVscaQ = pVscaQ;
			}

			return qres;
		}

		QRESULT StartVsink(free_stack_t& _FreeStack_, qcap2_video_sink_t** ppVsink) {
			QRESULT qres = QCAP_RS_SUCCESSFUL;

			switch(1) { case 1:
				const int nBuffers = 10;
				const ULONG nColorSpaceType = QCAP_COLORSPACE_TYPE_Y210;
				const ULONG nVideoWidth = 1920;
				const ULONG nVideoHeight = 1080;
				const BOOL bVideoIsInterleaved = TRUE;
				const double dVideoFrameRate = 50;

				qcap2_video_sink_t* pVsink = qcap2_video_sink_new();
				_FreeStack_ += [pVsink]() {
					LOG_ENTER("qcap2_video_sink_delete(pVsink)");
					qcap2_video_sink_delete(pVsink);
					LOG_LEAVE("qcap2_video_sink_delete(pVsink)");
				};

				qcap2_video_sink_set_backend_type(pVsink, QCAP2_VIDEO_SINK_BACKEND_TYPE_V4L2CAP);

				{
					std::shared_ptr<qcap2_video_format_t> pVideoFormat(
						qcap2_video_format_new(), qcap2_video_format_delete);

					qcap2_video_format_set_property(pVideoFormat.get(),
						nColorSpaceType, nVideoWidth,
						bVideoIsInterleaved ? nVideoHeight / 2 : nVideoHeight,
						bVideoIsInterleaved, dVideoFrameRate);

					qcap2_video_sink_set_video_format(pVsink, pVideoFormat.get());
				}

				qcap2_video_sink_set_frame_count(pVsink, nBuffers);
				qcap2_video_sink_set_v4l2_name(pVsink, "video1");

				qres = qcap2_video_sink_start(pVsink);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_video_sink_start() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
				_FreeStack_ += [pVsink]() {
					QRESULT qres;

					LOG_ENTER("qcap2_video_sink_stop(pVsink)");
					qres = qcap2_video_sink_stop(pVsink);
					LOG_LEAVE("qcap2_video_sink_stop(pVsink)");
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): qcap2_video_sink_stop() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					}
				};

				*ppVsink = pVsink;
			}

			return qres;
		}

		QRESULT StartVsrcQ(free_stack_t& _FreeStack_, qcap2_rcbuffer_queue_t* pVscaQ, qcap2_video_sink_t* pVsink) {
			QRESULT qres = QCAP_RS_SUCCESSFUL;

			switch(1) { case 1:
				const int nBuffers = 10;

				qcap2_event_t* pEvent;
				qres = NewEvent(_FreeStack_, &pEvent);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_event_start() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				pVsrcQ = qcap2_rcbuffer_queue_new();
				_FreeStack_ += [&]() {
					LOG_ENTER("qcap2_rcbuffer_queue_delete(pVsrcQ)");
					qcap2_rcbuffer_queue_delete(pVsrcQ);
					LOG_LEAVE("qcap2_rcbuffer_queue_delete(pVsrcQ)");
				};

				qcap2_rcbuffer_queue_set_max_buffers(pVsrcQ, nBuffers);
				qcap2_rcbuffer_queue_set_event(pVsrcQ, pEvent);

				qres = qcap2_rcbuffer_queue_start(pVsrcQ);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_rcbuffer_queue_start() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
				_FreeStack_ += [&]() {
					QRESULT qres;

					LOG_ENTER("qcap2_rcbuffer_queue_stop(pVsrcQ)");
					qres = qcap2_rcbuffer_queue_stop(pVsrcQ);
					LOG_LEAVE("qcap2_rcbuffer_queue_stop(pVsrcQ)");
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): qcap2_rcbuffer_queue_stop() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					}
				};

				ZzStatBitRate* pStats = new ZzStatBitRate();
				_FreeStack_ += [pStats]() {
					delete pStats;
				};
				pStats->log_prefix = "vsrc";
				pStats->Reset();

				std::ofstream* pLog = new std::ofstream("vsrc.txt");
				_FreeStack_ += [pLog]() {
					delete pLog;
				};

				qres = AddEventHandler(_FreeStack_, pEvent,
					std::bind(&self_t::OnVsrcQ, this, pStats, pLog, pVscaQ, pVsink));
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): AddEventHandler() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
			}

			return qres;
		}

		QRESULT StartAsink(free_stack_t& _FreeStack_, qcap2_audio_sink_t** ppAsink) {
			QRESULT qres = QCAP_RS_SUCCESSFUL;

			switch(1) { case 1:
				const ULONG nAudioChannels = 8;
				const ULONG nAudioBitsPerSample = 16;
				const ULONG nAudioSampleFrequency = 48000;
				const ULONG nPeriodTime = 10 * 1000; // 10ms
				const ULONG nBufferTime = nPeriodTime * 10; // 10 periods

				qcap2_audio_sink_t* pAsink = qcap2_audio_sink_new();
				_FreeStack_ += [pAsink]() {
					LOG_ENTER("qcap2_audio_sink_delete(pAsink)");
					qcap2_audio_sink_delete(pAsink);
					LOG_LEAVE("qcap2_audio_sink_delete(pAsink)");
				};

				qcap2_audio_sink_set_backend_type(pAsink, QCAP2_AUDIO_SINK_BACKEND_TYPE_V4L2CAP);

				{
					std::shared_ptr<qcap2_audio_format_t> pAudioFormat(
						qcap2_audio_format_new(), qcap2_audio_format_delete);

					qcap2_audio_format_set_property(pAudioFormat.get(),
						nAudioChannels, nAudioBitsPerSample, nAudioSampleFrequency);

					qcap2_audio_sink_set_audio_format(pAsink, pAudioFormat.get());
				}

				qcap2_audio_sink_set_v4l2_name(pAsink, "video2");
				qcap2_audio_sink_set_period_time(pAsink, nPeriodTime);
				qcap2_audio_sink_set_buffer_time(pAsink, nBufferTime);

				qres = qcap2_audio_sink_start(pAsink);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_audio_sink_start() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
				_FreeStack_ += [pAsink]() {
					QRESULT qres;

					LOG_ENTER("qcap2_audio_sink_stop(pAsink)");
					qres = qcap2_audio_sink_stop(pAsink);
					LOG_LEAVE("qcap2_audio_sink_stop(pAsink)");
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): qcap2_audio_sink_stop() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					}
				};

				*ppAsink = pAsink;
			}

			return qres;
		}

		QRESULT StartAsrcQ(free_stack_t& _FreeStack_, qcap2_audio_sink_t* pAsink) {
			QRESULT qres = QCAP_RS_SUCCESSFUL;

			switch(1) { case 1:
				qcap2_event_t* pEvent;
				qres = NewEvent(_FreeStack_, &pEvent);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_event_start() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				pAsrcQ = qcap2_rcbuffer_queue_new();
				_FreeStack_ += [&]() {
					LOG_ENTER("qcap2_rcbuffer_queue_delete(pAsrcQ)");
					qcap2_rcbuffer_queue_delete(pAsrcQ);
					LOG_LEAVE("qcap2_rcbuffer_queue_delete(pAsrcQ)");
				};

				qcap2_rcbuffer_queue_set_max_buffers(pAsrcQ, 16);
				qcap2_rcbuffer_queue_set_event(pAsrcQ, pEvent);

				qres = qcap2_rcbuffer_queue_start(pAsrcQ);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_rcbuffer_queue_start() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
				_FreeStack_ += [&]() {
					QRESULT qres;

					LOG_ENTER("qcap2_rcbuffer_queue_stop(pAsrcQ)");
					qres = qcap2_rcbuffer_queue_stop(pAsrcQ);
					LOG_LEAVE("qcap2_rcbuffer_queue_stop(pAsrcQ)");
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): qcap2_rcbuffer_queue_stop() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					}
				};

				ZzStatBitRate* pStats = new ZzStatBitRate();
				_FreeStack_ += [pStats]() {
					delete pStats;
				};
				pStats->log_prefix = "asrc";
				pStats->Reset();

				std::ofstream* pLog = new std::ofstream("asrc.txt");
				_FreeStack_ += [pLog]() {
					delete pLog;
				};

				qres = AddEventHandler(_FreeStack_, pEvent,
					std::bind(&self_t::OnAsrcQ, this, pStats, pLog, pAsink));
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): AddEventHandler() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
			}

			return qres;
		}

		static QRETURN QCAP_EXPORT _OnSignalRemoved( PVOID pDevice, ULONG nVideoInput, ULONG nAudioInput, PVOID pUserData ) {
			self_t* pThis = (self_t*)pUserData;

			return pThis->OnSignalRemoved(pDevice, nVideoInput, nAudioInput);
		}

		QRETURN OnSignalRemoved( PVOID pDevice, ULONG nVideoInput, ULONG nAudioInput ) {
			LOG_ENTER("_FreeStack_avsrc_.flush()");
			_FreeStack_avsrc_.flush();
			LOG_LEAVE("_FreeStack_avsrc_.flush()");

			return QCAP_RT_OK;
		}

		static QRETURN QCAP_EXPORT _OnFormatChanged( PVOID pDevice, ULONG nVideoInput, ULONG nAudioInput, ULONG nVideoWidth, ULONG nVideoHeight, BOOL bVideoIsInterleaved, double dVideoFrameRate, ULONG nAudioChannels, ULONG nAudioBitsPerSample, ULONG nAudioSampleFrequency, PVOID pUserData ) {
			self_t* pThis = (self_t*)pUserData;

			return pThis->OnFormatChanged(pDevice, nVideoInput, nAudioInput, nVideoWidth, nVideoHeight, bVideoIsInterleaved, dVideoFrameRate, nAudioChannels, nAudioBitsPerSample, nAudioSampleFrequency);
		}

		QRETURN OnFormatChanged( PVOID pDevice, ULONG nVideoInput, ULONG nAudioInput, ULONG nVideoWidth, ULONG nVideoHeight, BOOL bVideoIsInterleaved, double dVideoFrameRate, ULONG nAudioChannels, ULONG nAudioBitsPerSample, ULONG nAudioSampleFrequency ) {
			this->nVideoWidth = nVideoWidth;
			this->nVideoHeight = nVideoHeight;
			this->bVideoIsInterleaved = bVideoIsInterleaved;
			this->dVideoFrameRate = dVideoFrameRate;
			this->nAudioChannels = nAudioChannels;
			this->nAudioBitsPerSample = nAudioBitsPerSample;
			this->nAudioSampleFrequency = nAudioSampleFrequency;

			LOG_ENTER("_FreeStack_avsrc_.flush()");
			_FreeStack_avsrc_.flush();
			LOG_LEAVE("_FreeStack_avsrc_.flush()");

			if(nVideoWidth == 3840 && nVideoHeight == 2160 && bVideoIsInterleaved == FALSE && fabs(dVideoFrameRate - 50.0) <= 0.03) {
				StartAVSrc(_FreeStack_avsrc_);
			} else {
				LOGE("%s(%d): unexpected signal format!", __FUNCTION__, __LINE__);
			}

			return QCAP_RT_OK;
		}

		static QRETURN QCAP_EXPORT _OnVideoPreview( PVOID pDevice, double dSampleTime, BYTE * pFrameBuffer, ULONG nFrameBufferLen, PVOID pUserData ) {
			self_t* pThis = (self_t*)pUserData;

			return pThis->OnVideoPreview(pDevice, dSampleTime, pFrameBuffer, nFrameBufferLen);
		}

		QRETURN OnVideoPreview( PVOID pDevice, double dSampleTime, BYTE * pFrameBuffer, ULONG nFrameBufferLen ) {
			QRESULT qres = QCAP_RS_SUCCESSFUL;

			if(qcap2_block_lock_enter(pVsrcLock)) {
				switch(1) { case 1:
					qcap2_rcbuffer_t* pRCBuffer = qcap2_rcbuffer_cast(pFrameBuffer, nFrameBufferLen);
					if(! pRCBuffer) {
						LOGE("%s(%d): unexpected value, pRCBuffer=%p", __FUNCTION__, __LINE__, pRCBuffer);
						break;
					}

					std::shared_ptr<qcap2_av_frame_t> pAVFrame(
						(qcap2_av_frame_t*)qcap2_rcbuffer_lock_data(pRCBuffer),
						[pRCBuffer](qcap2_av_frame_t* p) {
							if(p) qcap2_rcbuffer_unlock_data(pRCBuffer);
						});
					if(! pAVFrame.get()) {
						LOGE("%s(%d): unexpected value, pAVFrame=%p", __FUNCTION__, __LINE__, pAVFrame.get());
						break;
					}

					// to embed frame counter
					qcap2_av_frame_set_pkt_pos(pAVFrame.get(), nVideoFrame++);

#if 1
					qres = qcap2_rcbuffer_queue_push(pVsrcQ, pRCBuffer);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): qcap2_rcbuffer_queue_push() failed, qres=%d", __FUNCTION__, __LINE__, qres);
						break;
					}
#endif
				}

				qcap2_block_lock_leave(pVsrcLock);
			}

			return QCAP_RT_OK;
		}

		static QRETURN QCAP_EXPORT _OnAudioPreview( PVOID pDevice, double dSampleTime, BYTE * pFrameBuffer, ULONG nFrameBufferLen, PVOID pUserData ) {
			self_t* pThis = (self_t*)pUserData;

			return pThis->OnAudioPreview(pDevice, dSampleTime, pFrameBuffer, nFrameBufferLen);
		}

		QRETURN OnAudioPreview( PVOID pDevice, double dSampleTime, BYTE * pFrameBuffer, ULONG nFrameBufferLen ) {
			QRESULT qres;

			if(qcap2_block_lock_enter(pAsrcLock)) {
				switch(1) { case 1:
					qcap2_rcbuffer_t* pRCBuffer = qcap2_rcbuffer_cast(pFrameBuffer, nFrameBufferLen);
					if(! pRCBuffer) {
						LOGE("%s(%d): unexpected value, pRCBuffer=%p", __FUNCTION__, __LINE__, pRCBuffer);
						break;
					}

					std::shared_ptr<qcap2_av_frame_t> pAVFrame(
						(qcap2_av_frame_t*)qcap2_rcbuffer_lock_data(pRCBuffer),
						[pRCBuffer](qcap2_av_frame_t* p) {
							if(p) qcap2_rcbuffer_unlock_data(pRCBuffer);
						});
					if(! pAVFrame.get()) {
						LOGE("%s(%d): unexpected value, pAVFrame=%p", __FUNCTION__, __LINE__, pAVFrame.get());
						break;
					}

					// to embed frame counter
					qcap2_av_frame_set_pkt_pos(pAVFrame.get(), nAudioFrame++);

#if 1
					qres = qcap2_rcbuffer_queue_push(pAsrcQ, pRCBuffer);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): qcap2_rcbuffer_queue_push() failed, qres=%d", __FUNCTION__, __LINE__, qres);
						break;
					}
#endif
				}

				qcap2_block_lock_leave(pAsrcLock);
			}

			return QCAP_RT_OK;
		}

		QRETURN OnVsrcQ(ZzStatBitRate* pStats, std::ofstream* pLog, qcap2_rcbuffer_queue_t* pVscaQ, qcap2_video_sink_t* pVsink) {
			QRESULT qres;
			cudaError_t cuerr;

			if(qcap2_debug_get(1) == 1) {
				LOGW("%s(%d): DEBUG", __FUNCTION__, __LINE__);

				qcap2_debug_set(1, 0);
				sleep(1);
			}

			switch(1) { case 1:
				const bool bVideoIsInterleaved = true;

				qcap2_rcbuffer_t* pRCBuffer;
				qres = qcap2_rcbuffer_queue_pop(pVsrcQ, &pRCBuffer);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_rcbuffer_queue_pop() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
				if(! pRCBuffer) {
					LOGE("%s(%d): unexpected value, pRCBuffer=%p", __FUNCTION__, __LINE__, pRCBuffer);
					break;
				}

				int64_t now = _clk();
				std::shared_ptr<qcap2_rcbuffer_t> pRCBuffer_(pRCBuffer, qcap2_rcbuffer_release);
				std::shared_ptr<qcap2_av_frame_t> pAVFrame(
					(qcap2_av_frame_t*)qcap2_rcbuffer_lock_data(pRCBuffer),
					[pRCBuffer](qcap2_av_frame_t* p) {
						if(p) qcap2_rcbuffer_unlock_data(pRCBuffer);
					});
				if(! pAVFrame.get()) {
					LOGE("%s(%d): unexpected value, pAVFrame=%p", __FUNCTION__, __LINE__, pAVFrame.get());
					break;
				}

				int64_t nPTS;
				qcap2_av_frame_get_pts(pAVFrame.get(), &nPTS);
				uint8_t* pSrcBuffer;
				int nSrcStride;
				qcap2_av_frame_get_buffer(pAVFrame.get(), &pSrcBuffer, &nSrcStride);
				int64_t nFrames;
				qcap2_av_frame_get_pkt_pos(pAVFrame.get(), &nFrames);

#if 1
				pStats->Log(this->nVideoWidth * this->nVideoHeight * 2 * 10, now);
#endif

#if 0
				uint8_t* pBuffer;
				int nStride;
				qcap2_av_frame_get_buffer(pAVFrame.get(), &pBuffer, &nStride);
				(*pLog) << nPTS << ',' << (int64_t)pBuffer << ',' << nStride << std::endl;
#endif

#if 1
				int32_t nVscaQSize = qcap2_rcbuffer_queue_get_buffer_count(pVscaQ);
				if(nVscaQSize < 5) {
					LOGW("pVscaQ: %d", nVscaQSize);
				}

				qcap2_rcbuffer_t* pRCBuffer1;
				qres = qcap2_rcbuffer_queue_pop(pVscaQ, &pRCBuffer1);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_rcbuffer_queue_pop() failed, qres=%d, nVscaQSize=%d", __FUNCTION__, __LINE__, qres, nVscaQSize);
					break;
				}
				if(! pRCBuffer1) {
					LOGE("%s(%d): unexpected value, pRCBuffer1=%p", __FUNCTION__, __LINE__, pRCBuffer1);
					break;
				}

				std::shared_ptr<qcap2_rcbuffer_t> pRCBuffer1_(pRCBuffer1, qcap2_rcbuffer_release);
				std::shared_ptr<qcap2_av_frame_t> pAVFrame1(
					(qcap2_av_frame_t*)qcap2_rcbuffer_lock_data(pRCBuffer1),
					[pRCBuffer1](qcap2_av_frame_t* p) {
						if(p) qcap2_rcbuffer_unlock_data(pRCBuffer1);
					});
				if(! pAVFrame1.get()) {
					LOGE("%s(%d): unexpected value, pAVFrame1=%p", __FUNCTION__, __LINE__, pAVFrame1.get());
					break;
				}

				uint8_t* pDstBuffer;
				int nDstStride;
				qcap2_av_frame_get_buffer(pAVFrame1.get(), &pDstBuffer, &nDstStride);

				int nFieldType = (bVideoIsInterleaved ?
					((nFrames % 2) == 0 ? QCAP2_FIELD_TOP : QCAP2_FIELD_BOTTOM) : QCAP2_FIELD_NONE);

				// LOGD("nFrames=%d, %d", (int)nFrames, nFieldType);

				int nWidth = this->nVideoWidth;
				int nHeight = (this->nVideoHeight >> 1);
				nSrcStride <<= 1;

				switch(nFieldType) {
				case QCAP2_FIELD_TOP:
					// LOGW("TOP, %p, %d, %p, %d", pSrcBuffer, nSrcStride, pDstBuffer, nDstStride);
					pSrcBuffer += nSrcStride;
					break;

				case QCAP2_FIELD_BOTTOM:
					// LOGW("BOTTOM, %p, %d, %p, %d", pSrcBuffer, nSrcStride, pDstBuffer, nDstStride);
					break;
				}
				nHeight >>= 1;
				nSrcStride <<= 1;

#if 0
				LOGW("%s(%d): %d, %d x %d, (%p, %d) => (%p, %d),(%p, %d), => (%p, %d)", __FUNCTION__, __LINE__,
					nFrames, nWidth, nHeight, pSrcBuffer, nSrcStride,
					pTmpBuffer, nTmpStride, pTmpBuffer1, nTmpStride1, pDstBuffer, nDstStride);
#endif

#if 1
				cuerr = zppiHoriDownscale2X_Y210((uchar1*)pSrcBuffer, nSrcStride,
					(uchar1*)pDstBuffer, nDstStride, nWidth, nHeight);
				if(cuerr != cudaSuccess) {
					LOGE("%s(%d): zppiHoriDownscale2X_Y210(), cuerr=%d", __FUNCTION__, __LINE__, cuerr);
					break;
				}
#endif

#if 0
				static bool t = false;
				if(! t) {
					{
						char fn[PATH_MAX];
						sprintf(fn, "%s %dx%d.y210", "src", nWidth, nHeight);
						std::ofstream out(fn, std::ios::binary);
						for(int i = 0;i < nHeight;i++) {
							out.write((char*)pSrcBuffer + nSrcStride * i, nWidth * 2 * sizeof(uint16_t));
						}
					}

					{
						char fn[PATH_MAX];
						sprintf(fn, "%s %dx%d.y210", "dst", nWidth / 2, nHeight);
						std::ofstream out(fn, std::ios::binary);
						for(int i = 0;i < nHeight;i++) {
							out.write((char*)pDstBuffer + nDstStride * i, nWidth / 2 * 2 * sizeof(uint16_t));
						}
					}

					t = true;
				}
#endif

				qcap2_av_frame_set_pts(pAVFrame1.get(), nPTS);
				qcap2_av_frame_set_field_type(pAVFrame1.get(), nFieldType);

#if 1
				qres = qcap2_video_sink_push(pVsink, pRCBuffer1);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_video_sink_push() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
#endif
#endif
			}

			return QCAP_RT_OK;
		}

		QRETURN OnAsrcQ(ZzStatBitRate* pStats, std::ofstream* pLog, qcap2_audio_sink_t* pAsink) {
			QRESULT qres;

			switch(1) { case 1:
				qcap2_rcbuffer_t* pRCBuffer;
				qres = qcap2_rcbuffer_queue_pop(pAsrcQ, &pRCBuffer);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_rcbuffer_queue_pop() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
				if(! pRCBuffer) {
					LOGE("%s(%d): unexpected value, pRCBuffer=%p", __FUNCTION__, __LINE__, pRCBuffer);
					break;
				}

				int64_t now = _clk();
				std::shared_ptr<qcap2_rcbuffer_t> pRCBuffer_(pRCBuffer, qcap2_rcbuffer_release);
				std::shared_ptr<qcap2_av_frame_t> pAVFrame(
					(qcap2_av_frame_t*)qcap2_rcbuffer_lock_data(pRCBuffer),
					[pRCBuffer](qcap2_av_frame_t* p) {
						if(p) qcap2_rcbuffer_unlock_data(pRCBuffer);
					});
				if(! pAVFrame.get()) {
					LOGE("%s(%d): unexpected value, pAVFrame=%p", __FUNCTION__, __LINE__, pAVFrame.get());
					break;
				}

				ULONG nChannels;
				ULONG nSampleFmt;
				ULONG nSampleFrequency;
				ULONG nFrameSize;
				qcap2_av_frame_get_audio_property(pAVFrame.get(), &nChannels, &nSampleFmt, &nSampleFrequency, &nFrameSize);

#if 1
				pStats->Log(nChannels * nFrameSize * 16, now);
#endif

#if 0
				int64_t nPTS;
				qcap2_av_frame_get_pts(pAVFrame.get(), &nPTS);

				uint8_t* pBuffer;
				int nStride;
				qcap2_av_frame_get_buffer(pAVFrame.get(), &pBuffer, &nStride);
				(*pLog) << nPTS << ',' << (int64_t)pBuffer << ',' << nStride << std::endl;
#endif

#if 1
				qres = qcap2_audio_sink_push(pAsink, pRCBuffer);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_audio_sink_push() failed, qres=%d", __FUNCTION__, __LINE__, qres);
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
	{
		modules_init_t _modules_init_;
		ret = App0::Main();
	}

	return ret;
}
