#include "qcap2.h"
#include "qcap.linux.h"
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
}

using namespace __test_deint__;
using __testkit__::wait_for_test_finish;
using __testkit__::free_stack_t;
using __testkit__::callback_t;
using __testkit__::TestCase;

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
					return true;
				}, 1000000LL, 10LL);
			}

			_FreeStack_main_.flush();
		}

		QRETURN OnStart(free_stack_t& _FreeStack_, QRESULT& qres) {
			switch(1) { case 1:
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
				const ULONG nColorSpaceType = QCAP_COLORSPACE_TYPE_YUY2;

				PVOID pDevice;
				qres = QCAP_CREATE("SC0710 PCI", 0, NULL, &pDevice, TRUE, TRUE);
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
				QCAP_SET_VIDEO_INPUT(pDevice, QCAP_INPUT_TYPE_HDMI);
				QCAP_SET_AUDIO_INPUT(pDevice, QCAP_INPUT_TYPE_EMBEDDED_AUDIO);

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

				qcap2_block_lock_leave(pAsrcLock);
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
