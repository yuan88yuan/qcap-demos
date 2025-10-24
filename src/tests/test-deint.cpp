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

		qcap2_block_lock_t* pVsrcLock;
		qcap2_block_lock_t* pAsrcLock;
		qcap2_rcbuffer_queue_t* pVsrcQ;
		qcap2_rcbuffer_queue_t* pAsrcQ;
		int64_t nVideoFrames;
		int64_t nAudioFrames;
		bool bSnapshot;

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
					case 's': case 'S':
						LOGI("Snapshot the video frame...");
						bSnapshot = true;
						break;
					}
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

				nVideoFrames = 0;
				nAudioFrames = 0;
				bSnapshot = false;

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
				qcap2_video_scaler_t* pVsca; // for deint
				qres = StartVsca(_FreeStack_, &pVsca);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): StartVsca() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				qres = StartVsrcQ(_FreeStack_, pVsca);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): StartVsrcQ() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				qres = StartAsrcQ(_FreeStack_);
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

		QRESULT StartVsca(free_stack_t& _FreeStack_, qcap2_video_scaler_t** ppVsca) {
			QRESULT qres = QCAP_RS_SUCCESSFUL;

			switch(1) { case 1:
				const int nBuffers = 8;
				const ULONG nColorSpaceType = QCAP_COLORSPACE_TYPE_YUY2;
				const ULONG nVideoFrameWidth = 1920;
				const ULONG nVideoFrameHeight = 1080;

				qcap2_rcbuffer_t** pRCBuffers = new qcap2_rcbuffer_t*[nBuffers];
				_FreeStack_ += [pRCBuffers]() {
					delete[] pRCBuffers;
				};
				for(int i = 0;i < nBuffers;i++) {
					qres = new_video_sysbuf(_FreeStack_, nColorSpaceType,
						nVideoFrameWidth, nVideoFrameHeight, &pRCBuffers[i]);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): new_video_sysbuf() failed, qres=%d", __FUNCTION__, __LINE__,qres);
						break;
					}
				}
				if(qres != QCAP_RS_SUCCESSFUL)
					break;

				qcap2_video_scaler_t* pVsca = qcap2_video_scaler_new();
				_FreeStack_ += [pVsca]() {
					LOG_ENTER("qcap2_video_scaler_delete(pVsca)");
					qcap2_video_scaler_delete(pVsca);
					LOG_LEAVE("qcap2_video_scaler_delete(pVsca)");
				};

				qcap2_video_scaler_set_backend_type(pVsca, QCAP2_VIDEO_SCALER_BACKEND_TYPE_FF_FILTER_GRAPH);
				qcap2_video_scaler_set_multithread(pVsca, false);
				qcap2_video_scaler_set_frame_count(pVsca, nBuffers);
				qcap2_video_scaler_set_buffers(pVsca, pRCBuffers);
				qcap2_video_scaler_set_frame_count(pVsca, nBuffers);

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

					LOG_ENTER("qcap2_video_scaler_stop(pVsca)");
					qres = qcap2_video_scaler_stop(pVsca);
					LOG_LEAVE("qcap2_video_scaler_stop(pVsca)");
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): qcap2_video_scaler_stop() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					}
				};

				*ppVsca = pVsca;
			}

			return qres;
		}

		QRESULT StartVsrcQ(free_stack_t& _FreeStack_, qcap2_video_scaler_t* pVsca) {
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
					std::bind(&self_t::OnVsrcQ, this, pStats, pLog, pVsca));
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): AddEventHandler() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
			}

			return qres;
		}

		QRESULT StartAsrcQ(free_stack_t& _FreeStack_) {
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
					std::bind(&self_t::OnAsrcQ, this, pStats, pLog));
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

			if(nVideoWidth == 1920 && nVideoHeight == 1080 && bVideoIsInterleaved == TRUE && fabs(dVideoFrameRate - 30.0) <= 0.03) {
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
					qcap2_av_frame_set_pkt_pos(pAVFrame.get(), nVideoFrames++);

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
					qcap2_av_frame_set_pkt_pos(pAVFrame.get(), nAudioFrames++);

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

		QRETURN OnVsrcQ(ZzStatBitRate* pStats, std::ofstream* pLog, qcap2_video_scaler_t* pVsca) {
			QRESULT qres;

			switch(1) { case 1:
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
				uint8_t* pBuffer;
				int nStride;
				qcap2_av_frame_get_buffer(pAVFrame.get(), &pBuffer, &nStride);
				int64_t nFrames;
				qcap2_av_frame_get_pkt_pos(pAVFrame.get(), &nFrames);

#if 1
				pStats->Log(this->nVideoWidth * this->nVideoHeight * 2 * 10, now);
#endif

#if 0
				(*pLog) << nPTS << ',' << (int64_t)pBuffer << ',' << nStride << ',' << nFrames << std::endl;
#endif

				qres = qcap2_video_scaler_push(pVsca, pRCBuffer);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_video_scaler_push() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				qcap2_rcbuffer_t* pRCBuffer1;
				qres = qcap2_video_scaler_pop(pVsca, &pRCBuffer1);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_video_scaler_pop() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
				if(! pRCBuffer1) {
					LOGE("%s(%d): unexpected value, pRCBuffer1=%p", __FUNCTION__, __LINE__, pRCBuffer1);
					break;
				}

				std::shared_ptr<qcap2_rcbuffer_t> pRCBuffer1_(pRCBuffer1, qcap2_rcbuffer_release);

				if(bSnapshot) {
					bSnapshot = false;

					LOG_ENTER("DoSnapshot()...");
					DoSnapshot(pRCBuffer, pRCBuffer1);
					LOG_LEAVE("DoSnapshot()...");
				}
			}

			return QCAP_RT_OK;
		}

		QRETURN OnAsrcQ(ZzStatBitRate* pStats, std::ofstream* pLog) {
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

				int64_t nPTS;
				qcap2_av_frame_get_pts(pAVFrame.get(), &nPTS);
				uint8_t* pBuffer;
				int nStride;
				qcap2_av_frame_get_buffer(pAVFrame.get(), &pBuffer, &nStride);
				ULONG nChannels;
				ULONG nSampleFmt;
				ULONG nSampleFrequency;
				ULONG nFrameSize;
				qcap2_av_frame_get_audio_property(pAVFrame.get(), &nChannels, &nSampleFmt, &nSampleFrequency, &nFrameSize);
				int64_t nFrames;
				qcap2_av_frame_get_pkt_pos(pAVFrame.get(), &nFrames);

#if 1
				pStats->Log(nChannels * nFrameSize * 16, now);
#endif

#if 0
				(*pLog) << nPTS << ',' << (int64_t)pBuffer << ',' << nStride << ',' << nFrames << std::endl;
#endif
			}

			return QCAP_RT_OK;
		}

		void DoSnapshot(qcap2_rcbuffer_t* pRCBuffer, qcap2_rcbuffer_t* pRCBuffer1) {
			QRESULT qres;

			switch(1) { case 1:
				qres = qcap2_save_raw_video_frame(pRCBuffer, "snapshot");
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_save_raw_video_frame() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				qres = qcap2_save_raw_video_frame(pRCBuffer1, "snapshot1");
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_save_raw_video_frame() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
			}
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
