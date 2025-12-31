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

ZZ_INIT_LOG("sc6f0-dante-demo");

int g_argc = 0;
char** g_argv = NULL;

ZZ_MODULE_DECL(__zz_log__);

using namespace __zz_clock__;

namespace __sc6f0_dante_demo__ {
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

using namespace __sc6f0_dante_demo__;
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

		void DoWork() {
			QRESULT qres;

			LOGD("%s::%s", typeid(self_t).name(), __FUNCTION__);

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

					switch(1) { case 1:
						break;
					}

					return true;
				}, 1000000LL, 10LL);
			}

			_FreeStack_main_.flush();
		}

		QRETURN OnStart(free_stack_t& _FreeStack_, QRESULT& qres) {
			switch(1) { case 1:
				qcap2_rcbuffer_t* pRCBuffer;
				qres = __testkit__::new_video_qdmabuf(_FreeStack_, 3840, 2160, QCAP_COLORSPACE_TYPE_NV12, PROT_WRITE | PROT_READ, &pRCBuffer);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): new_video_qdmabuf() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				LOGD("pRCBuffer=%p", pRCBuffer);

				qres = __testkit__::map_video_qdmabuf(pRCBuffer, PROT_WRITE | PROT_READ);
				_FreeStack_ += [pRCBuffer]() {
					__testkit__::unmap_video_qdmabuf(pRCBuffer);
				};

				qcap2_dmabuf_t* pDmabuf;
				qres = qcap2_rcbuffer_get_qdmabuf(pRCBuffer, &pDmabuf);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_rcbuffer_get_qdmabuf() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				LOGD("pDmabuf=%p, { fd=%d, dmabuf_size=%u, pVirAddr=%p, nPhyAddr=%p, nSize=%u}",
					pDmabuf, pDmabuf->fd, pDmabuf->dmabuf_size, pDmabuf->pVirAddr,
					pDmabuf->nPhyAddr, pDmabuf->nSize);

				qcap2_av_frame_t* pAVFrame = (qcap2_av_frame_t*)qcap2_rcbuffer_get_data(pRCBuffer);
				uint8_t* pBuffer[4];
				int pStride[4];
				qcap2_av_frame_get_buffer1(pAVFrame, pBuffer, pStride);

				LOGD("pAVFrame=%p, { %p/%p/%p/%p, %d/%d/%d/%d }",
					pAVFrame, pBuffer[0], pBuffer[1], pBuffer[2], pBuffer[3],
					pStride[0], pStride[1], pStride[2], pStride[3]);
			}
			return QCAP_RT_OK;
		}
	} mTestCase1;

	struct TestCase2 : public TestCase {
		typedef TestCase2 self_t;
		typedef TestCase super_t;

		ULONG nColorSpaceType;
		ULONG nVideoWidth;
		ULONG nVideoHeight;
		BOOL bVideoIsInterleaved;
		double dVideoFrameRate;

		int nSnapshot;

		void DoWork() {
			QRESULT qres;

			LOGD("%s::%s", typeid(self_t).name(), __FUNCTION__);

			// nColorSpaceType = QCAP_COLORSPACE_TYPE_Y444;
			// nColorSpaceType = QCAP_COLORSPACE_TYPE_NV16;
			nColorSpaceType = QCAP_COLORSPACE_TYPE_NV12;
			nVideoWidth = 3840;
			nVideoHeight = 2160;
			bVideoIsInterleaved = FALSE;
			dVideoFrameRate = 60.0;

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
				qcap2_event_t* pVsrcEvent;
				qcap2_video_source_t* pVsrc;
				qres = StartVsrc(_FreeStack_, &pVsrc, &pVsrcEvent);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): StartVsrc() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

#if 1
				qcap2_event_t* pVencEvent;
				qcap2_video_encoder_t* pVenc;
				qres = StartVenc(_FreeStack_, &pVenc, &pVencEvent);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): StartVenc() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
#endif

				qres = AddEventHandler(_FreeStack_, pVsrcEvent,
					std::bind(&self_t::OnVsrc, this, pVsrc, pVenc));
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): AddEventHandler() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

#if 1
				qres = AddEventHandler(_FreeStack_, pVencEvent,
					std::bind(&self_t::OnVenc, this, pVenc));
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): AddEventHandler() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
#endif
			}

			return QCAP_RT_OK;
		}

		QRESULT StartVsrc(free_stack_t& _FreeStack_, qcap2_video_source_t** ppVsrc, qcap2_event_t** ppEvent) {
			QRESULT qres = QCAP_RS_SUCCESSFUL;

			switch(1) { case 1:
				qcap2_event_t* pEvent;
				qres = NewEvent(_FreeStack_, &pEvent);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): NewEvent() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				qcap2_video_source_t* pVsrc = qcap2_video_source_new();
				_FreeStack_ += [pVsrc]() {
					qcap2_video_source_delete(pVsrc);
				};

				qcap2_video_source_set_backend_type(pVsrc, QCAP2_VIDEO_SOURCE_BACKEND_TYPE_V4L2);
				qcap2_video_source_set_v4l2_name(pVsrc, "video0");
				qcap2_video_source_set_frame_count(pVsrc, 4);
				qcap2_video_source_set_buf_type(pVsrc, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
				qcap2_video_source_set_memory(pVsrc, V4L2_MEMORY_MMAP);
				qcap2_video_source_set_exp_buf(pVsrc, true);
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

				qres = qcap2_video_source_start(pVsrc);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_video_source_start() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
				_FreeStack_ += [pVsrc]() {
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

		QRETURN OnVsrc(qcap2_video_source_t* pVsrc, qcap2_video_encoder_t* pVenc) {
			QRESULT qres;

			switch(1) { case 1:
				qcap2_rcbuffer_t* pRCBuffer;
				qres = qcap2_video_source_pop(pVsrc, &pRCBuffer);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_video_source_pop() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
				std::shared_ptr<qcap2_rcbuffer_t> pRCBuffer_(pRCBuffer,
					qcap2_rcbuffer_release);

				qres = qcap2_video_encoder_push(pVenc, pRCBuffer);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_video_encoder_push() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

#if 1
				if(nSnapshot > 0) {
					LOGD("--nSnapshot=%d", --nSnapshot);

					qres = qcap2_save_raw_video_frame(pRCBuffer, "snapshot");
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): qcap2_save_raw_video_frame() failed, qres=%d", __FUNCTION__, __LINE__, qres);
						break;
					}
				}
#endif
			}

			return QCAP_RT_OK;
		}

		QRESULT StartVenc(free_stack_t& _FreeStack_, qcap2_video_encoder_t** ppVenc, qcap2_event_t** ppEvent) {
			QRESULT qres = QCAP_RS_SUCCESSFUL;

			switch(1) { case 1:
				qcap2_event_t* pEvent;
				qres = NewEvent(_FreeStack_, &pEvent);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): NewEvent() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				qcap2_video_encoder_t* pVenc = qcap2_video_encoder_new();
				_FreeStack_ += [pVenc]() {
					qcap2_video_encoder_delete(pVenc);
				};

				{
					std::shared_ptr<qcap2_video_encoder_property_t> pVencProp(qcap2_video_encoder_property_new(),
						qcap2_video_encoder_property_delete);

					UINT nGpuNum = 0;
					ULONG nEncoderType = QCAP_ENCODER_TYPE_ALLEGRO2;
					ULONG nEncoderFormat = QCAP_ENCODER_FORMAT_H265;
					// ULONG nColorSpaceType = nColorSpaceType;
					ULONG nWidth = nVideoWidth;
					ULONG nHeight = nVideoHeight;
					double dFrameRate = dVideoFrameRate;
					ULONG nRecordProfile = QCAP_RECORD_PROFILE_MAIN;
					ULONG nRecordLevel = QCAP_RECORD_LEVEL_51;
					ULONG nRecordEntropy = QCAP_RECORD_ENTROPY_CABAC;
					ULONG nRecordComplexity = QCAP_RECORD_COMPLEXITY_0;
					ULONG nRecordMode = QCAP_RECORD_MODE_CBR;
					ULONG nQuality = 8000;
					ULONG nBitRate = 48 * 1000000;
					ULONG nGOP = 60;
					ULONG nBFrames = 0;
					BOOL bIsInterleaved = FALSE;
					ULONG nSlices = 0;
					ULONG nLayers = 0;
					ULONG nSceneCut = 0;
					BOOL bMultiThread = FALSE;
					BOOL bMBBRC = FALSE;
					BOOL bExtBRC = FALSE;
					ULONG nMinQP = 0;
					ULONG nMaxQP = 0;
					ULONG nVBVMaxRate = 0;
					ULONG nVBVBufSize = 0;
					ULONG nAspectRatioX = 0;
					ULONG nAspectRatioY = 0;

					qcap2_video_encoder_property_set_property1(pVencProp.get(), nGpuNum, nEncoderType, nEncoderFormat, nColorSpaceType, nWidth, nHeight, dFrameRate, nRecordProfile, nRecordLevel, nRecordEntropy, nRecordComplexity, nRecordMode, nQuality, nBitRate, nGOP, nBFrames, bIsInterleaved, nSlices, nLayers, nSceneCut, bMultiThread, bMBBRC, bExtBRC, nMinQP, nMaxQP, nVBVMaxRate , nVBVBufSize, nAspectRatioX, nAspectRatioY);
					qcap2_video_encoder_property_set_low_delay(pVencProp.get(), true);
					qcap2_video_encoder_property_set_time_scale_factor(pVencProp.get(), 1);
					qcap2_video_encoder_set_video_property(pVenc, pVencProp.get());
				}

				qcap2_video_encoder_set_multithread(pVenc, false);
				qcap2_video_encoder_set_num_cores(pVenc, 4);
				qcap2_video_encoder_set_filler_ctrl_mode(pVenc, AL_FILLER_ENC);
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

				*ppVenc = pVenc;
				*ppEvent = pEvent;
			}

			return qres;
		}

		QRETURN OnVenc(qcap2_video_encoder_t* pVenc) {
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
			}

			return QCAP_RT_OK;
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
