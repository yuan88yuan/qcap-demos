#include <stdlib.h>
#include <time.h>

#include "qcap2.h"
#include "qcap2.sipl.h"
#include "qcap2.cuda.h"

#include "ZzLog.h"
#include "ZzModules.h"
#include "ZzClock.h"
#include "ZzStats.h"
#include "ZzUtils.h"
#include "testkit.h"

ZZ_INIT_LOG("sipl-demo");

int g_argc = 0;
char** g_argv = NULL;

ZZ_MODULE_DECL(__zz_log__);

using namespace __zz_clock__;

namespace __sipl_demo__ {
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

using namespace __sipl_demo__;
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
		}
	}

	struct TestCase1 : public TestCase {
		typedef TestCase1 self_t;
		typedef TestCase super_t;

		ULONG nColorSpaceType;
		ULONG nVideoWidth;
		ULONG nVideoHeight;
		BOOL bVideoIsInterleaved;
		double dVideoFrameRate;

		bool bSnapshot;

		void DoWork() {
			QRESULT qres;

			LOGD("%s::%s", typeid(self_t).name(), __FUNCTION__);

			nColorSpaceType = QCAP_COLORSPACE_TYPE_YUY2;
			nVideoWidth = 3840;
			nVideoHeight = 2160;
			bVideoIsInterleaved = FALSE;
			dVideoFrameRate = 60;

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

			_FreeStack_main_.flush();
		}

		QRETURN OnStart(QRESULT& qres) {
			switch(1) { case 1:
				qcap2_event_t* pVsrcEvent;
				qcap2_video_source_t* pVsrc;
				qres = StartVsrc(&pVsrc, &pVsrcEvent);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): StartVsrc() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				qres = AddEventHandler(_FreeStack_evt_, pVsrcEvent,
					std::bind(&self_t::OnVsrc, this, pVsrc));
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): AddEventHandler() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
			}

			return QCAP_RT_OK;
		}

		QRESULT StartVsrc(qcap2_video_source_t** ppVsrc, qcap2_event_t** ppEvent) {
			QRESULT qres = QCAP_RS_SUCCESSFUL;

			switch(1) { case 1:
				qcap2_event_t* pEvent;
				qres = NewEvent(_FreeStack_evt_, &pEvent);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): NewEvent() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				qcap2_video_source_t* pVsrc = qcap2_video_source_new();
				_FreeStack_evt_ += [pVsrc]() {
					qcap2_video_source_delete(pVsrc);
				};

				qcap2_video_source_set_backend_type(pVsrc, QCAP2_VIDEO_SOURCE_BACKEND_TYPE_SIPL);
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

				qcap2_video_source_set_frame_count(pVsrc, 4);
				qcap2_video_source_set_config_file(pVsrc, "yuan_config.json");

				qres = qcap2_video_source_start(pVsrc);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_video_source_start() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
				_FreeStack_evt_ += [pVsrc]() {
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

#if 0
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
#endif
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
