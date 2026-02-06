#include "qcap.linux.h"
#include "qcap2.h"
#include "qcap2.user.h"

#include "ZzLog.h"
#include "ZzModules.h"
#include "ZzClock.h"
#include "ZzStats.h"
#include "ZzUtils.h"
#include "testkit.h"

ZZ_INIT_LOG("test-rcbuf");

int g_argc = 0;
char** g_argv = NULL;

ZZ_MODULE_DECL(__zz_log__);

using namespace __zz_clock__;
using __testkit__::wait_for_test_finish;
using __testkit__::TestCase;
using __testkit__::free_stack_t;
using __testkit__::tick_ctrl_t;
using __testkit__::NewEvent;
using __testkit__::spinlock_lock;
using __testkit__::spinlock_unlock;

namespace __test_rcbuf__ {
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

using namespace __test_rcbuf__;

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

		ULONG nColorSpaceType;
		ULONG nVideoWidth;
		ULONG nVideoHeight;

		int nTest;

		struct MyVideoFrame {
			int index;
			void* buffers[4];
			qcap2_av_frame_t av_frame;

			MyVideoFrame() {
				qcap2_av_frame_init(&av_frame);
				memset(buffers, 0, sizeof(buffers));
			}

			~MyVideoFrame() {
				for(int i = 0;i < 4;++i)
					free(buffers[i]);
			}

			static void _on_free_resource(PVOID pData) {
				MyVideoFrame* pThis = qcap2_container_of(pData, MyVideoFrame, av_frame);
				pThis->on_free_resource();
			}

			void on_free_resource() {
				uint8_t* pBuffer[4];
				int pStride[4];
				qcap2_av_frame_get_buffer1(&av_frame, pBuffer, pStride);

				LOGI("-%d: [%p/%p], [%d/%d]", index, pBuffer[0], pBuffer[1], pStride[0], pStride[1]);
			}
		};

		void DoWork() {
			QRESULT qres;

			LOGD("%s::%s", typeid(self_t).name(), __FUNCTION__);

			nColorSpaceType = QCAP_COLORSPACE_TYPE_NV12;
			nVideoWidth = 1920;
			nVideoHeight = 1080;
			nTest = 0;

			switch(1) { case 1:
				qres = StartEventHandlers();
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): StartEventHandlers() failed, qres=%d", __FUNCTION__, __LINE__, qres);
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
					case 't':
					case 'T':
						nTest++;
						LOGI("nTest=%d", nTest);
						break;
					}
					return true;
				}, 1000000LL, 10LL);
			}

			_FreeStack_main_.flush();
		}

		QRETURN OnStart(free_stack_t& _FreeStack_, QRESULT& qres) {
			switch(1) { case 1:
				qcap2_event_t* pEvent_vsrc;
				qres = NewEvent(_FreeStack_, &pEvent_vsrc);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): NewEvent() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				qcap2_video_source_t* pVsrc;
				qres = StartVsrc(_FreeStack_, pEvent_vsrc, &pVsrc);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): StartVsrc() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				qres = AddEventHandler(_FreeStack_, pEvent_vsrc,
					std::bind(&self_t::OnVsrc, this, pVsrc));
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): AddEventHandler() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
			}

			return QCAP_RT_OK;
		}

		QRESULT StartVsrc(free_stack_t& _FreeStack_, qcap2_event_t* pEvent_vsrc, qcap2_video_source_t** ppVsrc) {
			QRESULT qres;
			int err;

			switch(1) { case 1:
				const int nBuffers = 4;

				qcap2_rcbuffer_t** pBuffers = new qcap2_rcbuffer_t*[nBuffers];
				_FreeStack_ += [pBuffers]() {
					delete [] pBuffers;
				};

				for(int i = 0;i < nBuffers;i++) {
					MyVideoFrame* pVideoFrame = new MyVideoFrame();
					_FreeStack_ += [pVideoFrame]() {
						delete pVideoFrame;
					};

					pVideoFrame->index = i;

					qcap2_rcbuffer_t* pRCBuffer = qcap2_rcbuffer_new(&pVideoFrame->av_frame, MyVideoFrame::_on_free_resource);
					_FreeStack_ += [pRCBuffer]() {
						qcap2_rcbuffer_delete(pRCBuffer);
					};

					qcap2_av_frame_t* pAVFrame = (qcap2_av_frame_t*)qcap2_rcbuffer_get_data(pRCBuffer);
					qcap2_av_frame_set_video_property(pAVFrame, nColorSpaceType, nVideoWidth, nVideoHeight);

					int pStride[4];
					memset(pStride, 0, sizeof(pStride));
					pStride[0] = __testkit__::align((int)nVideoWidth, 16);
					pStride[1] = pStride[0];

					err = posix_memalign(&pVideoFrame->buffers[0], 16, pStride[0] * nVideoHeight);
					if(err) {
						LOGE("%s(%d): posix_memalign() failed, err=%d", __FUNCTION__, __LINE__, err);
						break;
					}
					err = posix_memalign(&pVideoFrame->buffers[1], 16, pStride[1] * nVideoHeight / 2);
					if(err) {
						LOGE("%s(%d): posix_memalign() failed, err=%d", __FUNCTION__, __LINE__, err);
						break;
					}

					uint8_t* pBuffer[4];
					memset(pBuffer, 0, sizeof(pBuffer));
					pBuffer[0] = (uint8_t*)pVideoFrame->buffers[0];
					pBuffer[1] = (uint8_t*)pVideoFrame->buffers[1];

					qcap2_av_frame_set_buffer1(&pVideoFrame->av_frame, pBuffer, pStride);
					LOGI("+%d: [%p/%p], [%d/%d]", i, pBuffer[0], pBuffer[1], pStride[0], pStride[1]);

					pBuffers[i] = pRCBuffer;
				}
				if(qres != QCAP_RS_SUCCESSFUL)
					break;

				qcap2_video_source_t* pVsrc = qcap2_video_source_new();
				_FreeStack_ += [pVsrc]() {
					qcap2_video_source_delete(pVsrc);
				};

				qcap2_video_source_set_backend_type(pVsrc, QCAP2_VIDEO_SOURCE_BACKEND_TYPE_TPG);
				qcap2_video_source_set_frame_count(pVsrc, nBuffers);
				qcap2_video_source_set_buffers(pVsrc, &pBuffers[0]);
				qcap2_video_source_set_event(pVsrc, pEvent_vsrc);

				{
					std::shared_ptr<qcap2_video_format_t> pVideoFormat(
						qcap2_video_format_new(), qcap2_video_format_delete);

					qcap2_video_format_set_property(pVideoFormat.get(),
						nColorSpaceType, nVideoWidth, nVideoHeight, FALSE, 1.0);

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
			}

			return qres;
		}

		QRETURN OnVsrc(qcap2_video_source_t* pVsrc) {
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
				std::shared_ptr<qcap2_av_frame_t> pAVFrame(
					(qcap2_av_frame_t*)qcap2_rcbuffer_lock_data(pRCBuffer),
					[pRCBuffer](qcap2_av_frame_t*) {
						qcap2_rcbuffer_unlock_data(pRCBuffer);
					});

				uint8_t* pBuffer[4];
				int pStride[4];
				qcap2_av_frame_get_buffer1(pAVFrame.get(), pBuffer, pStride);
				LOGI("OnVsrc: [%p/%p], [%d/%d]", pBuffer[0], pBuffer[1], pStride[0], pStride[1]);

				if(nTest > 0) {
					MyVideoFrame* pVideoFrame = qcap2_container_of(pAVFrame.get(), MyVideoFrame, av_frame);

					LOGI("!%d: qcap2_rcbuffer_add_ref", pVideoFrame->index);
					qcap2_rcbuffer_add_ref(pRCBuffer);
					nTest--;
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
