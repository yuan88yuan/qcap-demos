#include "qcap2.h"
#include "qcap2.nvbuf.h"
#include "qcap2.cuda.h"

#include "ZzLog.h"
#include "ZzModules.h"
#include "ZzClock.h"
#include "ZzStats.h"
#include "ZzUtils.h"
#include "testkit.h"

ZZ_INIT_LOG("bsic-demo");

int g_argc = 0;
char** g_argv = NULL;

ZZ_MODULE_DECL(__zz_log__);

using namespace __zz_clock__;

namespace __bsic_demo__ {
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

using namespace __bsic_demo__;
using __testkit__::wait_for_test_finish;
using __testkit__::TestCase;
using __testkit__::free_stack_t;

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

		case 3:
			mTestCase3.DoWork();
			break;

		case 4:
			mTestCase4.DoWork();
			break;

		case 5:
			mTestCase5.DoWork();
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

				qcap2_rcbuffer_t* pRCBuffer;
				qres = __testkit__::new_video_cudahostbuf(_FreeStack_, QCAP_COLORSPACE_TYPE_NV12, 1920, 1080, cudaHostAllocMapped, &pRCBuffer);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): __testkit__::new_video_cudahostbuf() failed, qres=%d", qres);
					break;
				}

				qres = qcap2_fill_video_test_pattern(pRCBuffer, QCAP2_TEST_PATTERN_0);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_fill_video_test_pattern() failed, qres=%d", qres);
					break;
				}

				qcap2_video_scaler_t* pVsca;
				qres = StartVsca(_FreeStack_, &pVsca);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): StartVsca() failed, qres=%d", qres);
					break;
				}

				qres = qcap2_video_scaler_push(pVsca, pRCBuffer);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_video_scaler_push() failed, qres=%d", qres);
					break;
				}

				qcap2_rcbuffer_t* pRCBuffer1;
				qres = qcap2_video_scaler_pop(pVsca, &pRCBuffer1);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_video_scaler_pop() failed, qres=%d", qres);
					break;
				}
				std::shared_ptr<qcap2_rcbuffer_t> pRCBuffer1_(pRCBuffer1, qcap2_rcbuffer_release);

				std::shared_ptr<qcap2_av_frame_t> pAVFrame1(
					(qcap2_av_frame_t*)qcap2_rcbuffer_lock_data(pRCBuffer1),
					[pRCBuffer1](qcap2_av_frame_t*) {
						qcap2_rcbuffer_unlock_data(pRCBuffer1);
					});

				qres = qcap2_av_frame_store_picture(pAVFrame1.get(), "testcase1.jpg");
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_av_frame_store_picture() failed, qres=%d", qres);
					break;
				}
			}

			_FreeStack_main_.flush();
		}

		QRESULT StartVsca(free_stack_t& _FreeStack_, qcap2_video_scaler_t** ppVsca) {
			QRESULT qres = QCAP_RS_SUCCESSFUL;

			switch(1) { case 1:
				const int nBuffers = 1;
				const ULONG nColorSpaceType = QCAP_COLORSPACE_TYPE_I420;
				const ULONG nVideoFrameWidth = 1920;
				const ULONG nVideoFrameHeight = 1080;

				qcap2_video_scaler_t* pVsca = qcap2_video_scaler_new();
				_FreeStack_ += [pVsca]() {
					qcap2_video_scaler_delete(pVsca);
				};

				qcap2_video_scaler_set_backend_type(pVsca, QCAP2_VIDEO_SCALER_BACKEND_TYPE_DEFAULT);
				qcap2_video_scaler_set_multithread(pVsca, false);
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

					qres = qcap2_video_scaler_stop(pVsca);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): qcap2_video_scaler_stop() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					}
				};

				*ppVsca = pVsca;
			}

			return qres;
		}
	} mTestCase1;

	struct TestCase2 : public TestCase {
		typedef TestCase2 self_t;
		typedef TestCase super_t;

		void DoWork() {
			QRESULT qres;

			LOGD("%s::%s", typeid(self_t).name(), __FUNCTION__);

			switch(1) { case 1:
				free_stack_t& _FreeStack_ = _FreeStack_main_;

				qcap2_rcbuffer_t* pRCBuffer;
				qres = __testkit__::new_video_sysbuf(_FreeStack_, QCAP_COLORSPACE_TYPE_YUY2, 1920, 1080, &pRCBuffer);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): __testkit__::new_video_sysbuf() failed, qres=%d", qres);
					break;
				}

				qres = qcap2_fill_video_test_pattern(pRCBuffer, QCAP2_TEST_PATTERN_0);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_fill_video_test_pattern() failed, qres=%d", qres);
					break;
				}

				qcap2_video_scaler_t* pVsca;
				qres = StartVsca(_FreeStack_, &pVsca);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): StartVsca() failed, qres=%d", qres);
					break;
				}

				qres = qcap2_video_scaler_push(pVsca, pRCBuffer);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_video_scaler_push() failed, qres=%d", qres);
					break;
				}

				qcap2_rcbuffer_t* pRCBuffer1;
				qres = qcap2_video_scaler_pop(pVsca, &pRCBuffer1);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_video_scaler_pop() failed, qres=%d", qres);
					break;
				}
				std::shared_ptr<qcap2_rcbuffer_t> pRCBuffer1_(pRCBuffer1, qcap2_rcbuffer_release);

				std::shared_ptr<qcap2_av_frame_t> pAVFrame1(
					(qcap2_av_frame_t*)qcap2_rcbuffer_lock_data(pRCBuffer1),
					[pRCBuffer1](qcap2_av_frame_t*) {
						qcap2_rcbuffer_unlock_data(pRCBuffer1);
					});

				qres = qcap2_av_frame_store_picture(pAVFrame1.get(), "testcase2.bmp");
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_av_frame_store_picture() failed, qres=%d", qres);
					break;
				}
			}

			_FreeStack_main_.flush();
		}

		QRESULT StartVsca(free_stack_t& _FreeStack_, qcap2_video_scaler_t** ppVsca) {
			QRESULT qres = QCAP_RS_SUCCESSFUL;

			switch(1) { case 1:
				const int nBuffers = 1;
				const ULONG nColorSpaceType = QCAP_COLORSPACE_TYPE_ARGB32;
				const ULONG nVideoFrameWidth = 1920;
				const ULONG nVideoFrameHeight = 1080;

				qcap2_video_scaler_t* pVsca = qcap2_video_scaler_new();
				_FreeStack_ += [pVsca]() {
					qcap2_video_scaler_delete(pVsca);
				};

				qcap2_video_scaler_set_backend_type(pVsca, QCAP2_VIDEO_SCALER_BACKEND_TYPE_DEFAULT);
				qcap2_video_scaler_set_multithread(pVsca, false);
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

					qres = qcap2_video_scaler_stop(pVsca);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): qcap2_video_scaler_stop() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					}
				};

				*ppVsca = pVsca;
			}

			return qres;
		}
	} mTestCase2;

	struct TestCase3 : public TestCase {
		typedef TestCase3 self_t;
		typedef TestCase super_t;

		void DoWork() {
			QRESULT qres;

			LOGD("%s::%s", typeid(self_t).name(), __FUNCTION__);

			switch(1) { case 1:
				free_stack_t& _FreeStack_ = _FreeStack_main_;

				NvBufSurfaceCreateParams oNVBufParam;

				memset(&oNVBufParam, 0, sizeof(oNVBufParam));
				oNVBufParam.width = 1920;
				oNVBufParam.height = 1080;
				oNVBufParam.layout = NVBUF_LAYOUT_PITCH;
				oNVBufParam.memType = NVBUF_MEM_DEFAULT;
				oNVBufParam.gpuId = 0;
				oNVBufParam.colorFormat = NVBUF_COLOR_FORMAT_NV12;

				qcap2_rcbuffer_t* pRCBuffer;
				qres = __testkit__::new_video_nvbuf(_FreeStack_, oNVBufParam, &pRCBuffer);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): __testkit__::new_video_nvbuf() failed, qres=%d", qres);
					break;
				}

				{
					qres = qcap2_rcbuffer_map_nvbuf(pRCBuffer, NVBUF_MAP_WRITE);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): qcap2_rcbuffer_map_nvbuf() failed, qres=%d", qres);
						break;
					}
					ZzUtils::Scoped ZZ_GUARD_NAME([pRCBuffer]() {
						QRESULT qres;

						qres = qcap2_rcbuffer_unmap_nvbuf(pRCBuffer);
						if(qres != QCAP_RS_SUCCESSFUL) {
							LOGE("%s(%d): qcap2_rcbuffer_unmap_nvbuf() failed, qres=%d", qres);
						}
					});

					qres = qcap2_fill_video_test_pattern(pRCBuffer, QCAP2_TEST_PATTERN_0);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): qcap2_fill_video_test_pattern() failed, qres=%d", qres);
						break;
					}
				}

				qcap2_video_scaler_t* pVsca;
				qres = StartVsca(_FreeStack_, &pVsca);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): StartVsca() failed, qres=%d", qres);
					break;
				}

				{
					qres = qcap2_rcbuffer_map_nvbuf(pRCBuffer, NVBUF_MAP_READ);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): qcap2_rcbuffer_map_nvbuf() failed, qres=%d", qres);
						break;
					}
					ZzUtils::Scoped ZZ_GUARD_NAME([pRCBuffer]() {
						QRESULT qres;

						qres = qcap2_rcbuffer_unmap_nvbuf(pRCBuffer);
						if(qres != QCAP_RS_SUCCESSFUL) {
							LOGE("%s(%d): qcap2_rcbuffer_unmap_nvbuf() failed, qres=%d", qres);
						}
					});

					qres = qcap2_video_scaler_push(pVsca, pRCBuffer);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): qcap2_video_scaler_push() failed, qres=%d", qres);
						break;
					}
				}

				qcap2_rcbuffer_t* pRCBuffer1;
				qres = qcap2_video_scaler_pop(pVsca, &pRCBuffer1);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_video_scaler_pop() failed, qres=%d", qres);
					break;
				}
				std::shared_ptr<qcap2_rcbuffer_t> pRCBuffer1_(pRCBuffer1, qcap2_rcbuffer_release);

				std::shared_ptr<qcap2_av_frame_t> pAVFrame1(
					(qcap2_av_frame_t*)qcap2_rcbuffer_lock_data(pRCBuffer1),
					[pRCBuffer1](qcap2_av_frame_t*) {
						qcap2_rcbuffer_unlock_data(pRCBuffer1);
					});

				qres = qcap2_av_frame_store_picture(pAVFrame1.get(), "testcase3.jpg");
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_av_frame_store_picture() failed, qres=%d", qres);
					break;
				}
			}

			_FreeStack_main_.flush();
		}

		QRESULT StartVsca(free_stack_t& _FreeStack_, qcap2_video_scaler_t** ppVsca) {
			QRESULT qres = QCAP_RS_SUCCESSFUL;

			switch(1) { case 1:
				const int nBuffers = 1;
				const ULONG nColorSpaceType = QCAP_COLORSPACE_TYPE_I420;
				const ULONG nVideoFrameWidth = 1920;
				const ULONG nVideoFrameHeight = 1080;

				qcap2_video_scaler_t* pVsca = qcap2_video_scaler_new();
				_FreeStack_ += [pVsca]() {
					qcap2_video_scaler_delete(pVsca);
				};

				qcap2_video_scaler_set_backend_type(pVsca, QCAP2_VIDEO_SCALER_BACKEND_TYPE_DEFAULT);
				qcap2_video_scaler_set_multithread(pVsca, false);
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

					qres = qcap2_video_scaler_stop(pVsca);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): qcap2_video_scaler_stop() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					}
				};

				*ppVsca = pVsca;
			}

			return qres;
		}
	} mTestCase3;

	struct TestCase4 : public TestCase {
		typedef TestCase4 self_t;
		typedef TestCase super_t;

		void DoWork() {
			QRESULT qres;

			LOGD("%s::%s", typeid(self_t).name(), __FUNCTION__);

			switch(1) { case 1:
				free_stack_t& _FreeStack_ = _FreeStack_main_;

				qcap2_rcbuffer_t* pRCBuffer;
				qres = __testkit__::new_video_cudahostbuf(_FreeStack_, QCAP_COLORSPACE_TYPE_NV12, 1920, 1080, cudaHostAllocMapped, &pRCBuffer);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): __testkit__::new_video_cudahostbuf() failed, qres=%d", qres);
					break;
				}

				qres = qcap2_fill_video_test_pattern(pRCBuffer, QCAP2_TEST_PATTERN_0);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_fill_video_test_pattern() failed, qres=%d", qres);
					break;
				}

				qcap2_video_scaler_t* pVsca;
				qres = StartVsca(_FreeStack_, &pVsca);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): StartVsca() failed, qres=%d", qres);
					break;
				}

				qres = qcap2_video_scaler_push(pVsca, pRCBuffer);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_video_scaler_push() failed, qres=%d", qres);
					break;
				}

				qcap2_rcbuffer_t* pRCBuffer1;
				qres = qcap2_video_scaler_pop(pVsca, &pRCBuffer1);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_video_scaler_pop() failed, qres=%d", qres);
					break;
				}
				std::shared_ptr<qcap2_rcbuffer_t> pRCBuffer1_(pRCBuffer1, qcap2_rcbuffer_release);

				qcap2_save_raw_video_frame(pRCBuffer1, "testcase4");
			}

			_FreeStack_main_.flush();
		}

		QRESULT StartVsca(free_stack_t& _FreeStack_, qcap2_video_scaler_t** ppVsca) {
			QRESULT qres = QCAP_RS_SUCCESSFUL;

			switch(1) { case 1:
				const int nBuffers = 1;
				const ULONG nColorSpaceType = QCAP_COLORSPACE_TYPE_GBRP;
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
	} mTestCase4;

	struct TestCase5 : public TestCase {
		typedef TestCase5 self_t;
		typedef TestCase super_t;

		void DoWork() {
			QRESULT qres;

			LOGD("%s::%s", typeid(self_t).name(), __FUNCTION__);

			switch(1) { case 1:
				free_stack_t& _FreeStack_ = _FreeStack_main_;

				qcap2_rcbuffer_t* pRCBuffer;
				qres = __testkit__::new_video_cudahostbuf(_FreeStack_, QCAP_COLORSPACE_TYPE_GBRP, 1920, 1080, cudaHostAllocMapped, &pRCBuffer);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): __testkit__::new_video_cudahostbuf() failed, qres=%d", qres);
					break;
				}

				qres = qcap2_fill_video_test_pattern(pRCBuffer, QCAP2_TEST_PATTERN_0);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_fill_video_test_pattern() failed, qres=%d", qres);
					break;
				}

				qcap2_video_scaler_t* pVsca;
				qres = StartVsca(_FreeStack_, &pVsca);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): StartVsca() failed, qres=%d", qres);
					break;
				}

				qres = qcap2_video_scaler_push(pVsca, pRCBuffer);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_video_scaler_push() failed, qres=%d", qres);
					break;
				}

				qcap2_rcbuffer_t* pRCBuffer1;
				qres = qcap2_video_scaler_pop(pVsca, &pRCBuffer1);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_video_scaler_pop() failed, qres=%d", qres);
					break;
				}
				std::shared_ptr<qcap2_rcbuffer_t> pRCBuffer1_(pRCBuffer1, qcap2_rcbuffer_release);

				qcap2_save_raw_video_frame(pRCBuffer1, "testcase5");
			}

			_FreeStack_main_.flush();
		}

		QRESULT StartVsca(free_stack_t& _FreeStack_, qcap2_video_scaler_t** ppVsca) {
			QRESULT qres = QCAP_RS_SUCCESSFUL;

			switch(1) { case 1:
				const int nBuffers = 1;
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
	} mTestCase5;
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
