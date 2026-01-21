#include <stdlib.h>
#include <time.h>

#include "qcap2.h"
#include "qcap2.coe.h"
#include "qcap2.sipl.h"
#include "qcap2.cuda.h"

#include "ZzLog.h"
#include "ZzModules.h"
#include "ZzClock.h"
#include "ZzStats.h"
#include "ZzUtils.h"
#include "testkit.h"

ZZ_INIT_LOG("coe-demo");

int g_argc = 0;
char** g_argv = NULL;

ZZ_MODULE_DECL(__zz_log__);

using namespace __zz_clock__;

namespace __coe_demo__ {
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

using namespace __coe_demo__;
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
				qres = ExecInEventHandlers(std::bind(&self_t::OnStart, this,
					std::ref(_FreeStack_evt_), std::ref(qres_evt)));
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

		QRETURN OnStart(free_stack_t& _FreeStack_, QRESULT& qres) {
			switch(1) { case 1:
				qcap2_event_t* pVsrcEvent;
				qcap2_video_source_t* pVsrc;
				qres = StartVsrc(_FreeStack_, &pVsrc, &pVsrcEvent);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): StartVsrc() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				qres = AddEventHandler(_FreeStack_, pVsrcEvent,
					std::bind(&self_t::OnVsrc, this, pVsrc));
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): AddEventHandler() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
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

				qcap2_video_source_set_backend_type(pVsrc, QCAP2_VIDEO_SOURCE_BACKEND_TYPE_COE);
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
				qcap2_video_source_set_verbosity(pVsrc, 1);

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

		QRETURN OnVsrc(qcap2_video_source_t* pVsrc) {
			QRESULT qres;
			NvSciError sciErr;
			cudaError_t cuErr;

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

				int64_t nPTS;
				qcap2_av_frame_get_pts(pAVFrame.get(), &nPTS);
				uint8_t* pBuffer;
				int nStride;
				qcap2_av_frame_get_buffer(pAVFrame.get(), &pBuffer, &nStride);
				int buffer_size = nVideoHeight * nStride;

				LOGD("nStride=%d, buffer_size=%d", nStride, buffer_size);

				nvsipl::INvSIPLClient::INvSIPLBuffer* pSIPLBuffer;
				qres = qcap2_rcbuffer_get_sipl_buffer(pRCBuffer, &pSIPLBuffer);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_video_source_pop() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				nvsipl::INvSIPLClient::INvSIPLNvMBuffer* pNvMBuffer = dynamic_cast<nvsipl::INvSIPLClient::INvSIPLNvMBuffer*>(pSIPLBuffer);
				if(! pNvMBuffer) {
					LOGE("%s(%d): dynamic_cast<nvsipl::INvSIPLClient::INvSIPLNvMBuffer*>(pSIPLBuffer) failed", __FUNCTION__, __LINE__);
					break;
				}

				NvSciBufObj sciBufObj = pNvMBuffer->GetNvSciBufImage();

#if 0
				NvSciBufAttrList bufAttrList = nullptr;
				sciErr = NvSciBufObjGetAttrList(sciBufObj, &bufAttrList);
				if (sciErr != NvSciError_Success) {
					LOGE("%s(%d): NvSciBufObjGetAttrList() failed, sciErr=%d", __FUNCTION__, __LINE__, sciErr);
					qres = QCAP_RS_ERROR_GENERAL;
					break;
				}

				NvSciBufAttrKeyValuePair imgAttrs[] = {
					{ NvSciBufImageAttrKey_PlaneColorFormat, NULL, 0 },
					{ NvSciBufImageAttrKey_SurfType, NULL, 0}
				};

				sciErr = NvSciBufAttrListGetAttrs(bufAttrList, imgAttrs, sizeof(imgAttrs) / sizeof(imgAttrs[0]));
				if (sciErr != NvSciError_Success) {
					LOGE("%s(%d): NvSciBufAttrListGetAttrs() failed, sciErr=%d", __FUNCTION__, __LINE__, sciErr);
					qres = QCAP_RS_ERROR_GENERAL;
					break;
				}

				NvSciBufAttrValColorFmt color_fmt = (imgAttrs[0].len != 0) ? *(static_cast<const NvSciBufAttrValColorFmt*>(imgAttrs[0].value)) : 0;
				NvSciBufSurfType surf_type = (imgAttrs[1].len != 0) ? *(static_cast<const NvSciBufSurfType*>(imgAttrs[1].value)) : 0;

				LOGD("color_fmt=%d, %d, surf_type=%d", color_fmt, NvSciColor_A8Y8U8V8, surf_type);
#endif

#if 1
				int fd_dmabuf;
				sciErr = NvSciBufObjGetDmaBufFd(sciBufObj, &fd_dmabuf);
				if (sciErr != NvSciError_Success) {
					LOGE("%s(%d): NvSciBufObjGetDmaBufFd() failed, sciErr=%d", __FUNCTION__, __LINE__, sciErr);
					break;
				}
				ZzUtils::Scoped _fd_dmabuf([fd_dmabuf]() {
					close(fd_dmabuf);
				});

				LOGD("fd_dmabuf=%d, buffer_size=%d", fd_dmabuf, buffer_size);

				cudaExternalMemoryHandleDesc memHandleDesc;
				memset(&memHandleDesc, 0, sizeof(memHandleDesc));

				memHandleDesc.type = cudaExternalMemoryHandleTypeOpaqueFd;
				memHandleDesc.handle.fd = fd_dmabuf;
				memHandleDesc.size = buffer_size;
				memHandleDesc.flags = 0;

				cudaExternalMemory_t extMem = nullptr;
				cuErr = cudaImportExternalMemory(&extMem, &memHandleDesc);
				if (cuErr != cudaSuccess) {
					LOGE("%s(%d): cudaImportExternalMemory() failed, cuErr=%d", __FUNCTION__, __LINE__, cuErr);
					break;
				}
#endif

#if 0
				cudaExternalMemoryHandleDesc memHandleDesc;
				memset(&memHandleDesc, 0, sizeof(memHandleDesc));

				memHandleDesc.type = cudaExternalMemoryHandleTypeNvSciBuf;
				memHandleDesc.handle.nvSciBufObject = sciBufObj;
				memHandleDesc.size = buffer_size;
				memHandleDesc.flags = 0;

				cudaExternalMemory_t extMem = nullptr;
				cuErr = cudaImportExternalMemory(&extMem, &memHandleDesc);
				if (cuErr != cudaSuccess) {
					LOGE("%s(%d): cudaImportExternalMemory() failed, cuErr=%d", __FUNCTION__, __LINE__, cuErr);
					break;
				}
#endif

				ZzUtils::Scoped _extMem([extMem]() {
					cudaDestroyExternalMemory(extMem);
				});

				LOGD("extMem=%p", extMem);

				if(bSnapshot) {
					bSnapshot = false;

					qres = qcap2_save_raw_video_frame(pRCBuffer, "snapshot");
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): qcap2_save_raw_video_frame() failed, qres=%d", __FUNCTION__, __LINE__, qres);
						break;
					}
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
