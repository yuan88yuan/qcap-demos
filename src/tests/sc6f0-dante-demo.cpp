#include <stdlib.h>
#include <time.h>

#include <atomic>
#include <drm.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>

#include "qcap2.h"
#include "qcap2.v4l2.h"
#include "qcap2.allegro2.h"

#include "ZzLog.h"
#include "ZzModules.h"
#include "ZzClock.h"
#include "ZzStats.h"
#include "ZzUtils.h"
#include "testkit.h"

extern "C" {
#include "dauservice.h"
}

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

	inline float mode_vrefresh(drmModeModeInfo *mode) {
		return mode->clock * 1000.00 / (mode->htotal * mode->vtotal);
	}

	static uint32_t get_prop_id(int fd, drmModeObjectProperties *props, std::shared_ptr<drmModePropertyRes>* prop_res, const char *name) {
		for (uint32_t i = 0; i < props->count_props; i++) {
			std::shared_ptr<drmModePropertyRes> p(drmModeGetProperty(fd, props->props[i]), drmModeFreeProperty);
			if (!strcmp(p->name, name)) {
				if(prop_res) *prop_res = p;
				return p->prop_id;
			}
		}

		return 0;
	}
}

using namespace __sc6f0_dante_demo__;
using __testkit__::wait_for_test_finish;
using __testkit__::TestCase;
using __testkit__::free_stack_t;
using __testkit__::tick_ctrl_t;
using __testkit__::NewEvent;
using __testkit__::AddEventHandler;
using __testkit__::spinlock_lock;
using __testkit__::spinlock_unlock;

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
			int err;

			switch(1) { case 1:
#if 1
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

				qcap2_video_sink_t* pVsink;
				qres = StartVsink(_FreeStack_, &pVsink);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): StartVsink() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				int* pVsinkQueue = new int;
				_FreeStack_ += [pVsinkQueue]() {
					delete pVsinkQueue;
				};
				*pVsinkQueue = 0;

				qres = AddEventHandler(_FreeStack_, pEvent_vsrc,
					std::bind(&self_t::OnVsrc, this, pVsrc, pVsink, pVsinkQueue));
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): AddEventHandler() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
#endif
			}

			return QCAP_RT_OK;
		}

		QRESULT StartVsrc(free_stack_t& _FreeStack_, qcap2_event_t* pEvent_vsrc, qcap2_video_source_t** ppVsrc) {
			QRESULT qres = QCAP_RS_SUCCESSFUL;

			switch(1) { case 1:
				const int nBuffers = 4;
				const ULONG nColorSpaceType = QCAP_COLORSPACE_TYPE_NV12;
				const ULONG nVideoFrameWidth = 1920;
				const ULONG nVideoFrameHeight = 1080;

				qcap2_video_source_t* pVsrc = qcap2_video_source_new();
				_FreeStack_ += [pVsrc]() {
					qcap2_video_source_delete(pVsrc);
				};

				qcap2_rcbuffer_t** pBuffers = new qcap2_rcbuffer_t*[nBuffers];
				_FreeStack_ += [pBuffers]() {
					delete [] pBuffers;
				};

				for(int i = 0;i < nBuffers;i++) {
					qcap2_rcbuffer_t* pRCBuffer;
					qres = __testkit__::new_video_qdmabuf(_FreeStack_, nVideoFrameWidth, nVideoFrameHeight, nColorSpaceType, PROT_WRITE | PROT_READ, &pRCBuffer);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): new_video_qdmabuf() failed, qres=%d", __FUNCTION__, __LINE__, qres);
						break;
					}

					qres = __testkit__::map_video_qdmabuf(pRCBuffer, PROT_WRITE | PROT_READ);
					ZzUtils::Scoped ZZ_GUARD_NAME([pRCBuffer]() {
						__testkit__::unmap_video_qdmabuf(pRCBuffer);
					});

					qcap2_dmabuf_t* pDmabuf;
					qres = qcap2_rcbuffer_get_qdmabuf(pRCBuffer, &pDmabuf);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): qcap2_rcbuffer_get_qdmabuf() failed, qres=%d", __FUNCTION__, __LINE__, qres);
						break;
					}

					std::shared_ptr<qcap2_av_frame_t> pAVFrame(
						(qcap2_av_frame_t*)qcap2_rcbuffer_lock_data(pRCBuffer),
						[pRCBuffer](qcap2_av_frame_t*) {
							qcap2_rcbuffer_unlock_data(pRCBuffer);
						});

					uint8_t* pData[4];
					int pLineSize[4];
					qcap2_av_frame_get_buffer1(pAVFrame.get(), pData, pLineSize);

					LOGD("%d: fd=%d [%p %p %p %p] [%d,%d,%d,%d]", i, pDmabuf->fd,
						pData[0], pData[1], pData[2], pData[3],
						(int)pLineSize[0], (int)pLineSize[1], (int)pLineSize[2], (int)pLineSize[3]);

#if 1
					// int nPattern = QCAP2_TEST_PATTERN_0;
					int nPattern = (i % 2 == 0 ? QCAP2_TEST_PATTERN_0 : QCAP2_TEST_PATTERN_1);
					qres = qcap2_fill_video_test_pattern(pRCBuffer, nPattern);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): qcap2_fill_video_test_pattern() failed, qres=%d", __FUNCTION__, __LINE__, qres);
						break;
					}
#else
					memcpy(pData[0], &strRawImage[0], nRawImageSize);
#endif

					if(i == 0) {
						qcap2_save_raw_video_frame(pRCBuffer, "tpg");
					}

					pBuffers[i] = pRCBuffer;
				}
				if(qres != QCAP_RS_SUCCESSFUL)
					break;

				qcap2_video_source_set_backend_type(pVsrc, QCAP2_VIDEO_SOURCE_BACKEND_TYPE_TPG);
				qcap2_video_source_set_frame_count(pVsrc, nBuffers);
				qcap2_video_source_set_buffers(pVsrc, &pBuffers[0]);
				qcap2_video_source_set_event(pVsrc, pEvent_vsrc);

				{
					std::shared_ptr<qcap2_video_format_t> pVideoFormat(
						qcap2_video_format_new(), qcap2_video_format_delete);

					qcap2_video_format_set_property(pVideoFormat.get(),
						nColorSpaceType, nVideoFrameWidth, nVideoFrameHeight, FALSE, 60.0);

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

		QRETURN OnVsrc(qcap2_video_source_t* pVsrc, qcap2_video_sink_t* pVsink, int* pVsinkQueue) {
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

#if 0
				uint8_t* pBuffer[4];
				int pStride[4];
				qcap2_av_frame_get_buffer1(pAVFrame.get(), pBuffer, pStride);
				LOGI("OnVsrc: [%p/%p], [%d/%d]", pBuffer[0], pBuffer[1], pStride[0], pStride[1]);
#endif

				qres = qcap2_video_sink_push(pVsink, pRCBuffer);
				if(qres != QCAP_RS_SUCCESSFUL) {
					// LOGE("%s(%d): qcap2_video_sink_push() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				if(*pVsinkQueue < 1) {
					(*pVsinkQueue)++;

					if(*pVsinkQueue == 1) {
						qres = qcap2_video_sink_run(pVsink);
						if(qres != QCAP_RS_SUCCESSFUL) {
							LOGE("%s(%d): qcap2_video_sink_run() failed, qres=%d", __FUNCTION__, __LINE__, qres);
							break;
						}
					}
				}
			}

			return QCAP_RT_OK;
		}

		QRESULT StartVsink(free_stack_t& _FreeStack_, qcap2_video_sink_t** ppVsink) {
			QRESULT qres = QCAP_RS_SUCCESSFUL;

			switch(1) { case 1:
				const int nBuffers = 4;
				const ULONG nColorSpaceType = QCAP_COLORSPACE_TYPE_NV12;
				const ULONG nVideoFrameWidth = 1920;
				const ULONG nVideoFrameHeight = 1080;

				qcap2_video_sink_t* pVsink = qcap2_video_sink_new();
				_FreeStack_ += [pVsink]() {
					qcap2_video_sink_delete(pVsink);
				};

				qcap2_video_sink_set_backend_type(pVsink, QCAP2_VIDEO_SINK_BACKEND_TYPE_V4L2);
				qcap2_video_sink_set_v4l2_name(pVsink, "video0");
				qcap2_video_sink_set_buf_type(pVsink, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
				qcap2_video_sink_set_memory(pVsink, V4L2_MEMORY_DMABUF);
				qcap2_video_sink_set_auto_run(pVsink, false);

				{
					std::shared_ptr<qcap2_video_format_t> pVideoFormat(
						qcap2_video_format_new(), qcap2_video_format_delete);

					qcap2_video_format_set_property(pVideoFormat.get(),
						nColorSpaceType, nVideoFrameWidth, nVideoFrameHeight, FALSE, 60);

					qcap2_video_sink_set_video_format(pVsink, pVideoFormat.get());
				}

				qres = qcap2_video_sink_start(pVsink);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_video_sink_start() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
				_FreeStack_ += [pVsink]() {
					QRESULT qres;

					qres = qcap2_video_sink_stop(pVsink);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): qcap2_video_sink_start() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					}
				};

				*ppVsink = pVsink;
			}

			return qres;
		}

	} mTestCase1;

	struct TestCase2 : public TestCase {
		typedef TestCase2 self_t;
		typedef TestCase super_t;

		std::shared_ptr<drmModePlane> mPlane;
		std::shared_ptr<drmModeCrtc> mCrtc;
		std::shared_ptr<drmModeConnector> mConnector;

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
			int err;

			switch(1) { case 1:
				const ULONG nVideoFrameWidth = 3840;
				const ULONG nVideoFrameHeight = 2160;
				const double dVideoFrameRate = 60.0;

				int drm_fd = drmOpen("xlnx", NULL);
				if(drm_fd < 0) {
					err = errno;
					LOGE("%s(%d): drmOpen() failed, err=%d", __FUNCTION__, __LINE__, err);
					break;
				}
				_FreeStack_ += [drm_fd]() {
					drmClose(drm_fd);
				};

				LOGW("drm_fd=%d", drm_fd);

				err = drmSetClientCap(drm_fd, DRM_CLIENT_CAP_ATOMIC, 1);
				if(err) {
					err = errno;
					LOGE("%s(%d): drmSetClientCap(DRM_CLIENT_CAP_ATOMIC) failed, err=%d", __FUNCTION__, __LINE__, err);
					break;
				}

				err = drmSetClientCap(drm_fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
				if(err) {
					err = errno;
					LOGE("%s(%d): drmSetClientCap(DRM_CLIENT_CAP_UNIVERSAL_PLANES) failed, err=%d", __FUNCTION__, __LINE__, err);
					break;
				}

				uint32_t nConnectorId = (uint32_t)44;
				uint32_t nCrtcId = (uint32_t)42;
				uint32_t nPrimaryPlaneId = (uint32_t)36;
				uint32_t nOverlayPlaneId = (uint32_t)34;

				std::shared_ptr<drmModeRes> pResources(drmModeGetResources(drm_fd), drmModeFreeResources);

				for(int i = 0;i < pResources->count_connectors;i++) {
					std::shared_ptr<drmModeConnector> pConnector(drmModeGetConnector(drm_fd, pResources->connectors[i]), drmModeFreeConnector);
					if(! pConnector) {
						qres = QCAP_RS_ERROR_INVALID_PARAMETER;
						LOGE("%s(%d): pConnector=%p", __FUNCTION__, __LINE__, pConnector.get());
						break;
					}

					if(nConnectorId == pConnector->connector_id) {
						mConnector = pConnector;
						break;
					}
				}
				if(! mConnector) {
					qres = QCAP_RS_ERROR_INVALID_PARAMETER;
					LOGE("%s(%d): unexpected value, nConnectorId=%u", __FUNCTION__, __LINE__, nConnectorId);
					break;
				}
				_FreeStack_ += [&]() {
					mConnector.reset();
				};

				LOGD("connector_id=%d connector_type=%d connector_type_id=%d connection=%d encoder_id=%d",
					(int)mConnector->connector_id, (int)mConnector->connector_type, (int)mConnector->connector_type_id,
					(int)mConnector->connection, (int)mConnector->encoder_id);

				if(! (mConnector->connector_type == DRM_MODE_CONNECTOR_HDMIA && mConnector->connection == DRM_MODE_CONNECTED)) {
					qres = QCAP_RS_ERROR_CONNECT_FAIL;
					LOGE("%s(%d): unexpected value, connector_type=%d connection=%d", __FUNCTION__, __LINE__,
						mConnector->connector_type, mConnector->connection);
					break;
				}

				drmModeModeInfoPtr pMode = NULL;
				for(int i = 0;i < mConnector->count_modes;i++) {
					drmModeModeInfoPtr pMode_ = &mConnector->modes[i];

					if(pMode_->hdisplay == nVideoFrameWidth &&
						pMode_->vdisplay == nVideoFrameHeight &&
						fabs(mode_vrefresh(pMode_) - dVideoFrameRate) < 0.005) {
						pMode = pMode_;
						break;
					}
				}
				if(! pMode) {
					qres = QCAP_RS_ERROR_INVALID_PARAMETER;
					LOGE("%s(%d): unexpected value, pMode=%p", __FUNCTION__, __LINE__, pMode);
					break;
				}

				LOGD("[%s] %d x %d @ %d (%.2f)", pMode->name, pMode->hdisplay, pMode->vdisplay, pMode->vrefresh, mode_vrefresh(pMode));

				uint32_t nModeBlobId;
				err = drmModeCreatePropertyBlob(drm_fd, pMode, sizeof(*pMode), &nModeBlobId);
				if(err) {
					err = errno;
					qres = QCAP_RS_ERROR_GENERAL;
					LOGE("%s(%d): drmModeCreatePropertyBlob() failed, err=%d", __FUNCTION__, __LINE__, err);
					break;
				}
				_FreeStack_ += [drm_fd, nModeBlobId]() {
					int err;

					err = drmModeDestroyPropertyBlob(drm_fd, nModeBlobId);
					if(err) {
						err = errno;
						LOGE("%s(%d): drmModeDestroyPropertyBlob() failed, err=%d", __FUNCTION__, __LINE__, err);
					}
				};

				for(int i = 0;i < pResources->count_crtcs;i++) {
					std::shared_ptr<drmModeCrtc> pCrtc(drmModeGetCrtc(drm_fd, pResources->crtcs[i]), drmModeFreeCrtc);
					if(! pCrtc) {
						qres = QCAP_RS_ERROR_INVALID_PARAMETER;
						LOGE("%s(%d): pCrtc=%p", __FUNCTION__, __LINE__, pCrtc.get());
						break;
					}

					if(nCrtcId == pCrtc->crtc_id) {
						mCrtc = pCrtc;
						break;
					}
				}
				if(! mCrtc) {
					qres = QCAP_RS_ERROR_INVALID_PARAMETER;
					LOGE("%s(%d): unexpected value, nCrtcId=%u", __FUNCTION__, __LINE__, nCrtcId);
					break;
				}

#if 0
				{
					struct drm_mode_create_dumb creq = { .height = nVideoFrameHeight, .width = nVideoFrameWidth, .bpp = 24 };
					err = drmIoctl(drm_fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq);
					if(err < 0) {
						qres = QCAP_RS_ERROR_GENERAL;
						err = errno;
						LOGE("%s(%d): drmIoctl(DRM_IOCTL_MODE_CREATE_DUMB) failed, err=%d", __FUNCTION__, __LINE__, err);
						break;
					}
					uint32_t bo = creq.handle;
					LOGD("bo=%d", bo);
					_FreeStack_ += [drm_fd, bo]() {
						int err;

						struct drm_mode_destroy_dumb dreq = { 0 };
						dreq.handle = bo;
						err = drmIoctl(drm_fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
						if(err < 0) {
							err = errno;
							LOGE("%s(%d): drmIoctl(DRM_IOCTL_MODE_DESTROY_DUMB) failed, err=%d", __FUNCTION__, __LINE__, err);
						}
					};

					uint32_t handles[4] = { creq.handle, 0, 0, 0 };
					uint32_t pitches[4] = { creq.width * 3, 0, 0, 0 };
					uint32_t offsets[4] = { 0, 0, 0, 0 };
					uint32_t fb;
					err = drmModeAddFB2(drm_fd, creq.width, creq.height, DRM_FORMAT_BGR888, handles, pitches, offsets, &fb, 0);
					if(err < 0) {
						qres = QCAP_RS_ERROR_GENERAL;
						err = errno;
						LOGE("%s(%d): drmModeAddFB2() failed, err=%d", __FUNCTION__, __LINE__, err);
						break;
					}
					LOGD("fb=%d", fb);
					_FreeStack_ += [drm_fd, fb]() {
						int err;

						err = drmModeRmFB(drm_fd, fb);
						if(err < 0) {
							err = errno;
							LOGE("%s(%d): drmModeRmFB() failed, err=%d", __FUNCTION__, __LINE__, err);
						}
					};

					std::shared_ptr<drmModeObjectProperties> props_crtc(
						drmModeObjectGetProperties(drm_fd, nCrtcId, DRM_MODE_OBJECT_CRTC), drmModeFreeObjectProperties);
					uint32_t prop_crtc_mode_id = get_prop_id(drm_fd, props_crtc.get(), NULL, "MODE_ID");
					uint32_t prop_crtc_active_id = get_prop_id(drm_fd, props_crtc.get(), NULL, "ACTIVE");

					std::shared_ptr<drmModeObjectProperties> props_conn(
						drmModeObjectGetProperties(drm_fd, nConnectorId, DRM_MODE_OBJECT_CONNECTOR), drmModeFreeObjectProperties);
					uint32_t prop_conn_crtc_id = get_prop_id(drm_fd, props_conn.get(), NULL, "CRTC_ID");

					std::shared_ptr<drmModeObjectProperties> props_plane(
						drmModeObjectGetProperties(drm_fd, nPrimaryPlaneId, DRM_MODE_OBJECT_PLANE), drmModeFreeObjectProperties);
					uint32_t prop_plane_crtc_id = get_prop_id(drm_fd, props_plane.get(), NULL, "CRTC_ID");
					uint32_t prop_plane_fb_id = get_prop_id(drm_fd, props_plane.get(), NULL, "FB_ID");
					uint32_t prop_plane_crtc_w = get_prop_id(drm_fd, props_plane.get(), NULL, "CRTC_W");
					uint32_t prop_plane_crtc_h = get_prop_id(drm_fd, props_plane.get(), NULL, "CRTC_H");
					uint32_t prop_plane_src_w = get_prop_id(drm_fd, props_plane.get(), NULL, "SRC_W");
					uint32_t prop_plane_src_h = get_prop_id(drm_fd, props_plane.get(), NULL, "SRC_H");

					std::shared_ptr<drmModeAtomicReq> req(drmModeAtomicAlloc(), drmModeAtomicFree);
					err = drmModeAtomicAddProperty(req.get(), nConnectorId, prop_conn_crtc_id, nCrtcId);
					LOGD("drmModeAtomicAddProperty(%d, %d, %d) = %d", nConnectorId, prop_conn_crtc_id, nCrtcId, err);

					err = drmModeAtomicAddProperty(req.get(), nCrtcId, prop_crtc_mode_id, nModeBlobId);
					LOGD("drmModeAtomicAddProperty(%d, %d, %d) = %d", nCrtcId, prop_crtc_mode_id, nModeBlobId, err);
					err = drmModeAtomicAddProperty(req.get(), nCrtcId, prop_crtc_active_id, 1);
					LOGD("drmModeAtomicAddProperty(%d, %d, %d) = %d", nCrtcId, prop_crtc_active_id, 1, err);

					err = drmModeAtomicAddProperty(req.get(), nPrimaryPlaneId, prop_plane_crtc_id, nCrtcId);
					LOGD("drmModeAtomicAddProperty(%d, %d, %d) = %d", nPrimaryPlaneId, prop_plane_crtc_id, nCrtcId, err);
					err = drmModeAtomicAddProperty(req.get(), nPrimaryPlaneId, prop_plane_fb_id, fb);
					LOGD("drmModeAtomicAddProperty(%d, %d, %d) = %d", nPrimaryPlaneId, prop_plane_fb_id, fb, err);
					err = drmModeAtomicAddProperty(req.get(), nPrimaryPlaneId, prop_plane_crtc_w, creq.width);
					LOGD("drmModeAtomicAddProperty(%d, %d, %d) = %d", nPrimaryPlaneId, prop_plane_crtc_w, creq.width, err);
					err = drmModeAtomicAddProperty(req.get(), nPrimaryPlaneId, prop_plane_crtc_h, creq.height);
					LOGD("drmModeAtomicAddProperty(%d, %d, %d) = %d", nPrimaryPlaneId, prop_plane_crtc_h, creq.height, err);
					err = drmModeAtomicAddProperty(req.get(), nPrimaryPlaneId, prop_plane_src_w, creq.width << 16);
					LOGD("drmModeAtomicAddProperty(%d, %d, %d) = %d", nPrimaryPlaneId, prop_plane_src_w, creq.width << 16, err);
					err = drmModeAtomicAddProperty(req.get(), nPrimaryPlaneId, prop_plane_src_h, creq.height << 16);
					LOGD("drmModeAtomicAddProperty(%d, %d, %d) = %d", nPrimaryPlaneId, prop_plane_src_h, creq.height << 16, err);

					err = drmModeAtomicCommit(drm_fd, req.get(), DRM_MODE_ATOMIC_ALLOW_MODESET, NULL);
					if(err < 0) {
						qres = QCAP_RS_ERROR_GENERAL;
						err = errno;
						LOGE("%s(%d): drmModeAtomicCommit(DRM_MODE_ATOMIC_ALLOW_MODESET) failed, err=%d", __FUNCTION__, __LINE__, err);
						break;
					}

					LOGD("Primary ACTIVE!");

#if 0
					// _FreeStack_ += [drm_fd, nPrimaryPlaneId, prop_plane_crtc_id, prop_plane_fb_id,
					// 	prop_plane_crtc_w, prop_plane_crtc_h, prop_plane_src_w, prop_plane_src_h]()
					{
						int err;

						std::shared_ptr<drmModeAtomicReq> req(drmModeAtomicAlloc(), drmModeAtomicFree);

						err = drmModeAtomicAddProperty(req.get(), nPrimaryPlaneId, prop_plane_crtc_id, 0);
						LOGD("drmModeAtomicAddProperty(%d, %d, %d) = %d", nPrimaryPlaneId, prop_plane_crtc_id, 0, err);
						err = drmModeAtomicAddProperty(req.get(), nPrimaryPlaneId, prop_plane_crtc_w, 0);
						LOGD("drmModeAtomicAddProperty(%d, %d, %d) = %d", nPrimaryPlaneId, prop_plane_crtc_w, 0, err);
						err = drmModeAtomicAddProperty(req.get(), nPrimaryPlaneId, prop_plane_crtc_h, 0);
						LOGD("drmModeAtomicAddProperty(%d, %d, %d) = %d", nPrimaryPlaneId, prop_plane_crtc_h, 0, err);
						err = drmModeAtomicAddProperty(req.get(), nPrimaryPlaneId, prop_plane_src_w, 0);
						LOGD("drmModeAtomicAddProperty(%d, %d, %d) = %d", nPrimaryPlaneId, prop_plane_src_w, 0, err);
						err = drmModeAtomicAddProperty(req.get(), nPrimaryPlaneId, prop_plane_src_h, 0);
						LOGD("drmModeAtomicAddProperty(%d, %d, %d) = %d", nPrimaryPlaneId, prop_plane_src_h, 0, err);
						err = drmModeAtomicAddProperty(req.get(), nPrimaryPlaneId, prop_plane_fb_id, 0);
						LOGD("drmModeAtomicAddProperty(%d, %d, %d) = %d", nPrimaryPlaneId, prop_plane_fb_id, 0, err);

						err = drmModeAtomicCommit(drm_fd, req.get(), DRM_MODE_ATOMIC_ALLOW_MODESET, NULL);
						if(err < 0) {
							err = errno;
							LOGE("%s(%d): drmModeAtomicCommit(DRM_MODE_ATOMIC_ALLOW_MODESET) failed, err=%d", __FUNCTION__, __LINE__, err);
						}

						LOGD("Primary DEACTIVE!");
					};
#endif
				}
#endif

#if 1
				{
					struct drm_mode_create_dumb creq = { .height = 1080, .width = 1920, .bpp = 12 };
					err = drmIoctl(drm_fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq);
					if(err < 0) {
						qres = QCAP_RS_ERROR_GENERAL;
						err = errno;
						LOGE("%s(%d): drmIoctl(DRM_IOCTL_MODE_CREATE_DUMB) failed, err=%d", __FUNCTION__, __LINE__, err);
						break;
					}
					uint32_t bo = creq.handle;
					LOGD("bo=%d, %dx%d %d", bo, creq.width, creq.height, creq.pitch);
					_FreeStack_ += [drm_fd, bo]() {
						int err;

						struct drm_mode_destroy_dumb dreq = { 0 };
						dreq.handle = bo;
						err = drmIoctl(drm_fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
						if(err < 0) {
							err = errno;
							LOGE("%s(%d): drmIoctl(DRM_IOCTL_MODE_DESTROY_DUMB) failed, err=%d", __FUNCTION__, __LINE__, err);
						}
					};

					uint32_t handles[4] = { creq.handle, creq.handle, 0, 0 };
					uint32_t pitches[4] = { creq.width, creq.width, 0, 0 };
					uint32_t offsets[4] = { 0, 0, 0, 0 };

					LOGD("{%d, %d}, {%d, %d},  {%d, %d}}", handles[0], handles[1],
						pitches[0], pitches[1], offsets[0], offsets[1]);

					uint32_t fb;
					err = drmModeAddFB2(drm_fd, creq.width, creq.height, DRM_FORMAT_NV12, handles, pitches, offsets, &fb, 0);
					if(err < 0) {
						qres = QCAP_RS_ERROR_GENERAL;
						err = errno;
						LOGE("%s(%d): drmModeAddFB2() failed, err=%d", __FUNCTION__, __LINE__, err);
						break;
					}
					LOGD("fb=%d", fb);
					_FreeStack_ += [drm_fd, fb]() {
						int err;

						err = drmModeRmFB(drm_fd, fb);
						if(err < 0) {
							err = errno;
							LOGE("%s(%d): drmModeRmFB() failed, err=%d", __FUNCTION__, __LINE__, err);
						}
					};

					std::shared_ptr<drmModeObjectProperties> props_plane(
						drmModeObjectGetProperties(drm_fd, nOverlayPlaneId, DRM_MODE_OBJECT_PLANE), drmModeFreeObjectProperties);
					uint32_t prop_plane_crtc_id = get_prop_id(drm_fd, props_plane.get(), NULL, "CRTC_ID");
					uint32_t prop_plane_fb_id = get_prop_id(drm_fd, props_plane.get(), NULL, "FB_ID");
					uint32_t prop_plane_crtc_w = get_prop_id(drm_fd, props_plane.get(), NULL, "CRTC_W");
					uint32_t prop_plane_crtc_h = get_prop_id(drm_fd, props_plane.get(), NULL, "CRTC_H");
					uint32_t prop_plane_src_w = get_prop_id(drm_fd, props_plane.get(), NULL, "SRC_W");
					uint32_t prop_plane_src_h = get_prop_id(drm_fd, props_plane.get(), NULL, "SRC_H");

					std::shared_ptr<drmModeAtomicReq> req(drmModeAtomicAlloc(), drmModeAtomicFree);

					err = drmModeAtomicAddProperty(req.get(), nOverlayPlaneId, prop_plane_crtc_id, nCrtcId);
					LOGD("drmModeAtomicAddProperty(%d, %d, %d) = %d", nOverlayPlaneId, prop_plane_crtc_id, nCrtcId, err);
					err = drmModeAtomicAddProperty(req.get(), nOverlayPlaneId, prop_plane_fb_id, fb);
					LOGD("drmModeAtomicAddProperty(%d, %d, %d) = %d", nOverlayPlaneId, prop_plane_fb_id, fb, err);
					err = drmModeAtomicAddProperty(req.get(), nOverlayPlaneId, prop_plane_crtc_w, nVideoFrameWidth);
					LOGD("drmModeAtomicAddProperty(%d, %d, %d) = %d", nOverlayPlaneId, prop_plane_crtc_w, nVideoFrameWidth, err);
					err = drmModeAtomicAddProperty(req.get(), nOverlayPlaneId, prop_plane_crtc_h, nVideoFrameHeight);
					LOGD("drmModeAtomicAddProperty(%d, %d, %d) = %d", nOverlayPlaneId, prop_plane_crtc_h, nVideoFrameHeight, err);
					err = drmModeAtomicAddProperty(req.get(), nOverlayPlaneId, prop_plane_src_w, creq.width << 16);
					LOGD("drmModeAtomicAddProperty(%d, %d, %d) = %d", nOverlayPlaneId, prop_plane_src_w, creq.width << 16, err);
					err = drmModeAtomicAddProperty(req.get(), nOverlayPlaneId, prop_plane_src_h, creq.height << 16);
					LOGD("drmModeAtomicAddProperty(%d, %d, %d) = %d", nOverlayPlaneId, prop_plane_src_h, creq.height << 16, err);

					err = drmModeAtomicCommit(drm_fd, req.get(), DRM_MODE_ATOMIC_ALLOW_MODESET, NULL);
					if(err < 0) {
						qres = QCAP_RS_ERROR_GENERAL;
						err = errno;
						LOGE("%s(%d): drmModeAtomicCommit(DRM_MODE_ATOMIC_ALLOW_MODESET) failed, err=%d", __FUNCTION__, __LINE__, err);
						break;
					}

					LOGD("Overlay ACTIVE!");

#if 0
					{
						int err;

						std::shared_ptr<drmModeAtomicReq> req(drmModeAtomicAlloc(), drmModeAtomicFree);

						err = drmModeAtomicAddProperty(req.get(), nOverlayPlaneId, prop_plane_crtc_id, 0);
						LOGD("drmModeAtomicAddProperty(%d, %d, %d) = %d", nOverlayPlaneId, prop_plane_crtc_id, 0, err);
						err = drmModeAtomicAddProperty(req.get(), nOverlayPlaneId, prop_plane_crtc_w, 0);
						LOGD("drmModeAtomicAddProperty(%d, %d, %d) = %d", nOverlayPlaneId, prop_plane_crtc_w, 0, err);
						err = drmModeAtomicAddProperty(req.get(), nOverlayPlaneId, prop_plane_crtc_h, 0);
						LOGD("drmModeAtomicAddProperty(%d, %d, %d) = %d", nOverlayPlaneId, prop_plane_crtc_h, 0, err);
						err = drmModeAtomicAddProperty(req.get(), nOverlayPlaneId, prop_plane_src_w, 0);
						LOGD("drmModeAtomicAddProperty(%d, %d, %d) = %d", nOverlayPlaneId, prop_plane_src_w, 0, err);
						err = drmModeAtomicAddProperty(req.get(), nOverlayPlaneId, prop_plane_src_h, 0);
						LOGD("drmModeAtomicAddProperty(%d, %d, %d) = %d", nOverlayPlaneId, prop_plane_src_h, 0, err);
						err = drmModeAtomicAddProperty(req.get(), nOverlayPlaneId, prop_plane_fb_id, 0);
						LOGD("drmModeAtomicAddProperty(%d, %d, %d) = %d", nOverlayPlaneId, prop_plane_fb_id, 0, err);

						err = drmModeAtomicCommit(drm_fd, req.get(), DRM_MODE_ATOMIC_ALLOW_MODESET, NULL);
						if(err < 0) {
							err = errno;
							LOGE("%s(%d): drmModeAtomicCommit(DRM_MODE_ATOMIC_ALLOW_MODESET) failed, err=%d", __FUNCTION__, __LINE__, err);
						}

						LOGD("Overlay DEACTIVE!");
					};
#endif
				}
#endif
			}

			return QCAP_RT_OK;
		}
	} mTestCase2;

	struct TestCase3 : public TestCase {
		typedef TestCase3 self_t;
		typedef TestCase super_t;

		free_stack_t _FreeStack_vsrc_;

		int nSnapshot;

		std::atomic_flag current_video_format_spinlock = ATOMIC_FLAG_INIT;
		struct video_format current_video_format;

		std::atomic_flag current_audio_info_spinlock = ATOMIC_FLAG_INIT;
		struct audio_info current_audio_info;

		dau_service* pHdmiRxDauServ;
		bool bDmxStarted;

		static void _get_cable_status(struct dau_service *dau,
						 struct DBusMessage *message,
						 void *ctxt)
		{
			self_t* pThis = (self_t*)ctxt;

			pThis->get_cable_status(dau, message);
		}

		void get_cable_status(struct dau_service *dau,
						 struct DBusMessage *message)
		{
			LOGI("---tag---: %s", __FUNCTION__);

			spinlock_lock(current_video_format_spinlock);
			bool locked = current_video_format.locked;
			spinlock_unlock(current_video_format_spinlock);

			LOGI("locked: %d", locked);

			dau_reply_get_cable_status(dau, message, locked);

			// StartDmx();
		}

		static void _get_hdcp(struct dau_service *dau,
						struct DBusMessage *message,
						void *ctxt)
		{
			LOGI("---tag---: %s", __FUNCTION__);

			struct hdcp hdcp;

			memset(&hdcp, 0x00, sizeof(hdcp));
			hdcp.current = HDCP_VERSION_22;
			hdcp.hdcp14_supported = false;
			hdcp.hdcp22_supported = true;

			dau_reply_get_hdcp(dau, message, &hdcp);
		}

		static void _set_hdcp(struct dau_service *dau,
						struct DBusMessage *message,
						enum hdcp_version version,
						void *ctxt)
		{
			LOGI("---tag---: %s", __FUNCTION__);

			dau_reply_success(dau, message);
		}

		static void _get_video_format(struct dau_service *dau,
						 struct DBusMessage *message,
						 void *ctxt)
		{
			self_t* pThis = (self_t*)ctxt;

			pThis->get_video_format(dau, message);
		}

		void get_video_format(struct dau_service *dau, struct DBusMessage *message) {
			LOGI("---tag---: %s", __FUNCTION__);

			video_format format;

			spinlock_lock(current_video_format_spinlock);
			format = current_video_format;
			spinlock_unlock(current_video_format_spinlock);

			dau_reply_get_video_format(dau, message, &format);
		}

		static void _get_audio_info(struct dau_service *dau,
						   struct DBusMessage *message,
						   void *ctxt)
		{
			self_t* pThis = (self_t*)ctxt;

			pThis->get_audio_info(dau, message);
		}

		void get_audio_info(struct dau_service *dau, struct DBusMessage *message) {
			LOGI("---tag---: %s", __FUNCTION__);

			audio_info info;

			spinlock_lock(current_audio_info_spinlock);
			info = current_audio_info;
			spinlock_unlock(current_audio_info_spinlock);

			dau_reply_get_audio_info(dau, message, &info);
		}

		static void _get_hdr(struct dau_service *dau,
					struct DBusMessage *message,
					void *ctxt)
		{
			LOGI("---tag---: %s", __FUNCTION__);

			char hdr[HDR_LEN] = { 0x00, 0x01, 0x02 };
			char dv[DV_LEN] = { 0x04, 0x05, 0x06 };
			struct hdr_info info = {
				.avi_c = 1,
				.avi_ec = 2,
				.avi_q = 3,
				.hdr = hdr,
				.dv = dv
			};

			dau_reply_get_hdr(dau, message, &info);
		}

		static void _set_edid(struct dau_service *dau,
						struct DBusMessage *message,
						const void *edid,
						void *ctxt)
		{
			LOGI("---tag---: %s", __FUNCTION__);

			dau_reply_success(dau, message);
		}

		static void _toggle_hpd(struct dau_service *dau,
					   struct DBusMessage *message,
					   void *ctxt)
		{
			LOGI("---tag---: %s", __FUNCTION__);

			dau_reply_success(dau, message);
		}

		static void _set_video_format(struct dau_service *dau,
						 struct DBusMessage *message,
						 const struct video_format *format,
						 void *ctxt)
		{
			LOGI("---tag---: %s", __FUNCTION__);

			dau_reply_success(dau, message);
		}

		static void _set_audio_info(struct dau_service *dau,
						   struct DBusMessage *message,
						   const struct audio_info *info,
						   void *ctxt)
		{
			LOGI("---tag---: %s", __FUNCTION__);

			dau_reply_success(dau, message);
		}

		static void _set_hdr(struct dau_service *dau,
					struct DBusMessage *message,
					const struct hdr_info *hdr,
					void *ctxt)
		{
			LOGI("---tag---: %s", __FUNCTION__);

			dau_reply_success(dau, message);
		}

		static void _get_edid(struct dau_service *dau,
					 struct DBusMessage *message,
					 void *ctxt)
		{
			LOGI("---tag---: %s", __FUNCTION__);

			unsigned char edid[EDID_LEN];
			unsigned int i;

			for (i = 0; i < sizeof(edid); ++i)
				edid[i] = i;

			dau_reply_get_edid(dau, message, edid);
		}

		void DoWork() {
			QRESULT qres;

			LOGD("%s::%s", typeid(self_t).name(), __FUNCTION__);

			memset(&current_video_format, 0, sizeof(current_video_format));
			memset(&current_audio_info, 0, sizeof(current_audio_info));
			nSnapshot = 0;
			bDmxStarted = false;

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

					switch(ch) {
					case 's': case 'S':
						LOGD("++nSnapshot=%d", ++nSnapshot);
						break;

					case 'd': case 'D':
						StartDmx();
						break;

					case 'c': case 'C':
						LOGI("---tag---: +dau_signal_cable_change");
						dau_signal_cable_change(pHdmiRxDauServ);
						LOGI("---tag---: -dau_signal_cable_change");
						break;

					case 'a': case 'A':
						LOGI("---tag---: +dau_signal_audio_change");
						dau_signal_audio_change(pHdmiRxDauServ);
						LOGI("---tag---: -dau_signal_audio_change");
						break;

					case 'v': case 'V':
						LOGI("---tag---: +dau_signal_video_change");
						dau_signal_video_change(pHdmiRxDauServ);
						LOGI("---tag---: -dau_signal_video_change");
						break;
					}

					return true;
				}, 1000000LL, 10LL);
			}

			_FreeStack_main_.flush();
		}

		QRETURN OnStart(free_stack_t& _FreeStack_, QRESULT& qres) {
			switch(1) { case 1:
				dau_service* pHdmiRxDauServ;
				qres = StartDauServ(_FreeStack_, &pHdmiRxDauServ);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): StartDauServ() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				this->pHdmiRxDauServ = pHdmiRxDauServ;

				dau_signal_cable_change(pHdmiRxDauServ);

				StartDmx();
			}
			return QCAP_RT_OK;
		}

		QRESULT StartDauServ(free_stack_t& _FreeStack_, dau_service** ppHdmiRxDauServ) {
			QRESULT qres = QCAP_RS_SUCCESSFUL;

			switch(1) { case 1:
				static const struct dau_service_methods methods = {
					.get_cable_status = _get_cable_status,
					.get_hdcp = _get_hdcp,
					.set_hdcp = _set_hdcp,

					.get_video_format = _get_video_format,
					.get_audio_info = _get_audio_info,
					.get_hdr = _get_hdr,
					.set_edid = _set_edid,
					.toggle_hpd = _toggle_hpd,

					.set_video_format = _set_video_format,
					.set_audio_info = _set_audio_info,
					.set_hdr = _set_hdr,
					.get_edid = _get_edid,
				};

				struct dau_service* hdmirx = dau_service_register(DAU_SERVICE_SOURCE, 0, &methods, this);
				if (! hdmirx) {
					qres = QCAP_RS_ERROR_GENERAL;
					LOGE("%s(%d): dau_service_register() failed", __FUNCTION__, __LINE__);
					break;
				}
				_FreeStack_ += [hdmirx]() {
					dau_service_unregister(hdmirx);
				};

				int fd_hdmirx = dau_service_get_fd(hdmirx);
				LOGD("fd_hdmirx=%d", fd_hdmirx);

				qres = AddEventHandler(_FreeStack_, fd_hdmirx, std::bind(&self_t::OnHdmiRxEvent, this, hdmirx));
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): AddEventHandler() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				*ppHdmiRxDauServ = hdmirx;
			}

			return qres;
		}

		QRETURN OnHdmiRxEvent(dau_service* dauserv) {
			// LOGI("---tag---: %s +dau_service_dispatch", __FUNCTION__);
			dau_service_dispatch(dauserv, 0);
			// LOGI("---tag---: %s -dau_service_dispatch", __FUNCTION__);

			return QCAP_RT_OK;
		}

		void StartDmx() {
			QRESULT qres;
			free_stack_t& _FreeStack_ = _FreeStack_evt_;

			if(! bDmxStarted) switch(1) { case 1:
				qcap2_event_t* pDmxEvent;
				qcap2_demuxer_t* pDmx;
				qres = StartDmx(_FreeStack_, &pDmx, &pDmxEvent);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): StartDmx() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				qres = AddEventHandler(_FreeStack_, pDmxEvent, std::bind(&self_t::OnDmx, this, pDmx,
					pHdmiRxDauServ));
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): AddEventHandler() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				_FreeStack_ += [&]() {
					_FreeStack_vsrc_.flush();
				};

				bDmxStarted = true;
			}
		}

		QRESULT StartDmx(free_stack_t& _FreeStack_, qcap2_demuxer_t** ppDmx, qcap2_event_t** ppDmxEvent) {
			QRESULT qres;

			switch(1) { case 1:
				qcap2_event_t* pEvent;
				qres = NewEvent(_FreeStack_, &pEvent);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): NewEvent() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				qcap2_demuxer_t* pDmx = qcap2_demuxer_new();
				_FreeStack_ += [pDmx]() {
					qcap2_demuxer_delete(pDmx);
				};

				qcap2_demuxer_set_type(pDmx, QCAP2_DEMUXER_TYPE_SC6F0);
				qcap2_demuxer_set_event(pDmx, pEvent);

				qres = qcap2_demuxer_start(pDmx);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_demuxer_start() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
				_FreeStack_ += [pDmx]() {
					QRESULT qres;

					qres = qcap2_demuxer_stop(pDmx);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): qcap2_demuxer_stop() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					}
				};

				*ppDmx = pDmx;
				*ppDmxEvent = pEvent;
			}

			return qres;
		}

		QRETURN OnDmx(qcap2_demuxer_t* pDmx, dau_service* pHdmiRxDauServ) {
			QRESULT qres;

			switch(1) {	case 1:
				free_stack_t& _FreeStack_ = _FreeStack_vsrc_;

				// to stop a/v src/codec which are running
				_FreeStack_.flush();

				qres = qcap2_demuxer_update(pDmx);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_demuxer_update() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				LOGD("%d VSRCs, %d ASRCs, %d PROGs",
					qcap2_demuxer_get_video_source_count(pDmx),
					qcap2_demuxer_get_audio_source_count(pDmx),
					qcap2_demuxer_get_program_count(pDmx));

				qcap2_program_info_t* pProgram = qcap2_demuxer_get_program_info(pDmx, 0);
				int nVideoIndex = qcap2_program_info_get_video_source_index(pProgram, 0);
				int nAudioIndex = qcap2_program_info_get_audio_source_index(pProgram, 0);

				LOGD("Prog[%s/%s] V:%d, A:%d", qcap2_program_info_get_metadata(pProgram, "service_name"),
					qcap2_program_info_get_metadata(pProgram, "service_provider"), nVideoIndex, nAudioIndex);

				ULONG nSrcColorSpaceType = QCAP_COLORSPACE_TYPE_UNDEFINED;
				const ULONG nColorSpaceType = QCAP_COLORSPACE_TYPE_NV12;
				const ULONG nVideoEncoderFormat = QCAP_ENCODER_FORMAT_H264;
				const ULONG nVideoBitRate = 60 * 1000000;
				ULONG nVideoWidth = 0;
				ULONG nVideoHeight = 0;
				BOOL bVideoIsInterleaved;
				double dVideoFrameRate;
				ULONG nAudioChannels = 0;
				ULONG nAudioBitsPerSample = 0;
				ULONG nAudioSampleFrequency = 0;
				const ULONG nAudioEncoderFormat = QCAP_ENCODER_FORMAT_AAC_ADTS;

				qcap2_event_t* pVsrcEvent = NULL;
				qcap2_event_t* pAsrcEvent = NULL;

				qcap2_video_encoder_t* pVenc = NULL;
				qcap2_event_t* pVencEvent = NULL;
				qcap2_audio_encoder_t* pAenc = NULL;
				qcap2_event_t* pAencEvent = NULL;

				qcap2_video_source_t* pVsrc = qcap2_demuxer_get_video_source(pDmx, nVideoIndex);
				std::shared_ptr<qcap2_video_format_t> pVideoFormat(qcap2_video_format_new(), qcap2_video_format_delete);
				qcap2_video_source_get_video_format(pVsrc, pVideoFormat.get());
				{
					qcap2_video_format_get_property(pVideoFormat.get(), &nSrcColorSpaceType, &nVideoWidth, &nVideoHeight, &bVideoIsInterleaved, &dVideoFrameRate);

					if(nSrcColorSpaceType == QCAP_COLORSPACE_TYPE_UNDEFINED || nVideoWidth <= 0 || nVideoHeight <= 0) {
						LOGI("v: no-link");

						spinlock_lock(current_video_format_spinlock);
						memset(&current_video_format, 0, sizeof(current_video_format));
						spinlock_unlock(current_video_format_spinlock);
					} else {
						LOGI("v: %08X %ux%u'%u, %.2f", nSrcColorSpaceType, nVideoWidth, nVideoHeight, bVideoIsInterleaved, dVideoFrameRate);

						spinlock_lock(current_video_format_spinlock);
						current_video_format = (video_format){
							.width = (unsigned int)nVideoWidth,
							.height = (unsigned int)nVideoHeight,
							.framerate = (unsigned int)dVideoFrameRate,
							.colorformat = VIDEO_COLORFORMAT_YUV444,
							.bpp = 8,
							.interlaced = (bVideoIsInterleaved != FALSE),
							.locked = true
						};
						spinlock_unlock(current_video_format_spinlock);

						qres = StartVsrc(_FreeStack_, pVsrc, nColorSpaceType,
							nVideoWidth, nVideoHeight, bVideoIsInterleaved, dVideoFrameRate, &pVsrcEvent);
						if(qres != QCAP_RS_SUCCESSFUL) {
							LOGE("%s(%d): StartVsrc() failed, qres=%d", __FUNCTION__, __LINE__, qres);
							break;
						}

#if 1
						qres = StartVenc(_FreeStack_, nColorSpaceType, nVideoWidth, nVideoHeight,
							bVideoIsInterleaved, dVideoFrameRate, nVideoEncoderFormat, nVideoBitRate, &pVenc, &pVencEvent);
						if(qres != QCAP_RS_SUCCESSFUL) {
							LOGE("%s(%d): StartVenc() failed, qres=%d", __FUNCTION__, __LINE__, qres);
							break;
						}
#endif
					}

					// LOGI("---tag---: +dau_signal_cable_change, hdmirx");
					dau_signal_cable_change(pHdmiRxDauServ);
					// LOGI("---tag---: -dau_signal_cable_change, hdmirx");

					// LOGI("---tag---: +dau_signal_video_change");
					dau_signal_video_change(pHdmiRxDauServ);
					// LOGI("---tag---: -dau_signal_video_change");
				}

				qcap2_audio_source_t* pAsrc = qcap2_demuxer_get_audio_source(pDmx, nAudioIndex);
				std::shared_ptr<qcap2_audio_format_t> pAudioFormat(qcap2_audio_format_new(), qcap2_audio_format_delete);
				qcap2_audio_source_get_audio_format(pAsrc, pAudioFormat.get());
				{
					qcap2_audio_format_get_property(pAudioFormat.get(), &nAudioChannels, &nAudioBitsPerSample, &nAudioSampleFrequency);

					if(nAudioChannels == 0 || nAudioBitsPerSample == 0 || nAudioSampleFrequency == 0) {
						LOGI("a: no-link");

						spinlock_lock(current_audio_info_spinlock);
						memset(&current_audio_info, 0, sizeof(current_audio_info));
						spinlock_unlock(current_audio_info_spinlock);
					} else {
						LOGI("a: %ux%u'%u", nAudioChannels, nAudioBitsPerSample, nAudioSampleFrequency);

						spinlock_lock(current_audio_info_spinlock);
						current_audio_info = (audio_info) {
							.channel_count = (unsigned int)nAudioChannels,
							.speaker_map = AUDIO_SM_FR_FL,
							.level_shift_value = 0,
							.downmix_inhibit = false,
							.lfe_playback_level = 0,
							.sample_rate = (unsigned int)nAudioSampleFrequency,
							.encoding = AUDIO_EN_LPCM,
							.sample_bits = (unsigned int)nAudioBitsPerSample
						};
						spinlock_unlock(current_audio_info_spinlock);

#if 1
						qres = StartAsrc(_FreeStack_, pAsrc, &pAsrcEvent);
						if(qres != QCAP_RS_SUCCESSFUL) {
							LOGE("%s(%d): StartAsrc() failed, qres=%d", __FUNCTION__, __LINE__, qres);
							break;
						}
#endif

#if 1
						qres = StartAenc(_FreeStack_, nAudioChannels, nAudioBitsPerSample,
							nAudioSampleFrequency, nAudioEncoderFormat, &pAenc, &pAencEvent);
						if(qres != QCAP_RS_SUCCESSFUL) {
							LOGE("%s(%d): StartAenc() failed, qres=%d", __FUNCTION__, __LINE__, qres);
							break;
						}
#endif
					}

					// LOGI("---tag---: +dau_signal_audio_change");
					dau_signal_audio_change(pHdmiRxDauServ);
					// LOGI("---tag---: -dau_signal_audio_change");
				}

				// next level muxers
				qcap2_video_decoder_t* pVdec_rtsp = NULL;
				qcap2_audio_decoder_t* pAdec_rtsp = NULL;
				PVOID pDanteServer = NULL;
				PVOID pDanteSender = NULL;
				if(nSrcColorSpaceType != QCAP_COLORSPACE_TYPE_UNDEFINED && nVideoWidth > 0 && nVideoHeight > 0) {
#if 1
					qcap2_muxer_t* pRTSPMuxer;
					qres = StartRTSPMuxer(_FreeStack_, nColorSpaceType, nVideoWidth, nVideoHeight,
						bVideoIsInterleaved, dVideoFrameRate, nVideoEncoderFormat, nVideoBitRate,
						nAudioChannels, nAudioBitsPerSample, nAudioSampleFrequency, nAudioEncoderFormat,
						&pRTSPMuxer, &pVdec_rtsp, &pAdec_rtsp);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): StartRTSPMuxer() failed, qres=%d", __FUNCTION__, __LINE__, qres);
						break;
					}
#endif

#if 1
					qres = StartDanteServer(_FreeStack_, nColorSpaceType, nVideoWidth, nVideoHeight,
						bVideoIsInterleaved, dVideoFrameRate, nVideoEncoderFormat, nVideoBitRate,
						nAudioChannels, nAudioBitsPerSample, nAudioSampleFrequency, nAudioEncoderFormat,
						&pDanteServer, &pDanteSender);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): StartDanteServer() failed, qres=%d", __FUNCTION__, __LINE__, qres);
						break;
					}
#endif

					if(pVsrcEvent) {
						qres = AddEventHandler(_FreeStack_, pVsrcEvent,
							std::bind(&self_t::OnVsrc, this, pVsrc, pVenc));
						if(qres != QCAP_RS_SUCCESSFUL) {
							LOGE("%s(%d): AddEventHandler() failed, qres=%d", __FUNCTION__, __LINE__, qres);
							break;
						}
					}

					if(pAsrcEvent) {
						qres = AddEventHandler(_FreeStack_, pAsrcEvent,
							std::bind(&self_t::OnAsrc, this, pAsrc, pAenc, pDanteSender));
						if(qres != QCAP_RS_SUCCESSFUL) {
							LOGE("%s(%d): AddEventHandler() failed, qres=%d", __FUNCTION__, __LINE__, qres);
							break;
						}
					}


					if(pVencEvent) {
						qres = AddEventHandler(_FreeStack_, pVencEvent,
							std::bind(&self_t::OnVenc, this, pVenc, pVdec_rtsp, pDanteSender));
						if(qres != QCAP_RS_SUCCESSFUL) {
							LOGE("%s(%d): AddEventHandler() failed, qres=%d", __FUNCTION__, __LINE__, qres);
							break;
						}
					}

					if(pAencEvent) {
						qres = AddEventHandler(_FreeStack_, pAencEvent,
							std::bind(&self_t::OnAenc, this, pAenc, pAdec_rtsp));
						if(qres != QCAP_RS_SUCCESSFUL) {
							LOGE("%s(%d): AddEventHandler() failed, qres=%d", __FUNCTION__, __LINE__, qres);
							break;
						}
					}
				}
			}

			return QCAP_RT_OK;
		}

		QRESULT StartVsrc(free_stack_t& _FreeStack_, qcap2_video_source_t* pVsrc, ULONG nColorSpaceType,
			ULONG nVideoWidth, ULONG nVideoHeight, BOOL bVideoIsInterleaved, double dVideoFrameRate, qcap2_event_t** ppEvent) {
			QRESULT qres = QCAP_RS_SUCCESSFUL;

			switch(1) { case 1:
				qcap2_event_t* pEvent;
				qres = NewEvent(_FreeStack_, &pEvent);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): NewEvent() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				qcap2_video_source_set_frame_count(pVsrc, 4);
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

				if(pVenc) {
					qres = qcap2_video_encoder_push(pVenc, pRCBuffer);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): qcap2_video_encoder_push() failed, qres=%d", __FUNCTION__, __LINE__, qres);
						break;
					}
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

		QRESULT StartVenc(free_stack_t& _FreeStack_, ULONG nColorSpaceType, ULONG nVideoWidth, ULONG nVideoHeight,
			BOOL bVideoIsInterleaved, double dVideoFrameRate, ULONG nEncoderFormat, ULONG nVideoBitRate,
			qcap2_video_encoder_t** ppVenc, qcap2_event_t** ppEvent) {
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
					// ULONG nEncoderFormat = QCAP_ENCODER_FORMAT_H265;
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
					ULONG nBitRate = nVideoBitRate;
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

		QRETURN OnVenc(qcap2_video_encoder_t* pVenc, qcap2_video_decoder_t* pVdec_rtsp, PVOID pDanteSender) {
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

				if(pVdec_rtsp) {
					qres = qcap2_video_decoder_push(pVdec_rtsp, pRCBuffer);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): qcap2_video_decoder_push() failed, qres=%d", __FUNCTION__, __LINE__, qres);
						break;
					}
				}

				if(pDanteSender) {
					BYTE * pStreamBuffer;
					ULONG nStreamBufferLen;
					qcap2_rcbuffer_to_buffer(pRCBuffer, &pStreamBuffer, &nStreamBufferLen);

					std::shared_ptr<qcap2_av_packet_t> pAVPacket((qcap2_av_packet_t*)qcap2_rcbuffer_lock_data(pRCBuffer),
						[pRCBuffer](qcap2_av_packet_t*) {
							qcap2_rcbuffer_unlock_data(pRCBuffer);
						}
					);

					int nStreamIndex;
					BOOL bIsKeyFrame;
					qcap2_av_packet_get_property(pAVPacket.get(), &nStreamIndex, &bIsKeyFrame);

					double dSampleTime;
					qcap2_av_packet_get_sample_time(pAVPacket.get(), &dSampleTime);

					qres = QCAP_SET_VIDEO_BROADCAST_SERVER_COMPRESSION_BUFFER(pDanteSender, 0, pStreamBuffer, nStreamBufferLen, bIsKeyFrame, dSampleTime);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): QCAP_SET_VIDEO_BROADCAST_SERVER_COMPRESSION_BUFFER() failed, qres=%d", __FUNCTION__, __LINE__, qres);
						break;
					}
				}
			}

			return QCAP_RT_OK;
		}

		QRESULT StartAsrc(free_stack_t& _FreeStack_, qcap2_audio_source_t* pAsrc, qcap2_event_t** ppEvent) {
			QRESULT qres = QCAP_RS_SUCCESSFUL;

			switch(1) { case 1:
				qcap2_event_t* pEvent;
				qres = NewEvent(_FreeStack_, &pEvent);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): NewEvent() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				qcap2_audio_source_set_event(pAsrc, pEvent);

				qres = qcap2_audio_source_start(pAsrc);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_audio_source_start() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
				_FreeStack_ += [pAsrc]() {
					QRESULT qres;

					qres = qcap2_audio_source_stop(pAsrc);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): qcap2_audio_source_stop() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					}
				};

				*ppEvent = pEvent;
			}

			return qres;
		}

		QRETURN OnAsrc(qcap2_audio_source_t* pAsrc, qcap2_audio_encoder_t* pAenc, PVOID pDanteSender) {
			QRESULT qres;

			switch(1) { case 1:
				qcap2_rcbuffer_t* pRCBuffer;
				qres = qcap2_audio_source_pop(pAsrc, &pRCBuffer);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_audio_source_pop() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
				std::shared_ptr<qcap2_rcbuffer_t> pRCBuffer_(pRCBuffer,
					qcap2_rcbuffer_release);

				if(pAenc) {
					qres = qcap2_audio_encoder_push(pAenc, pRCBuffer);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): qcap2_audio_encoder_push() failed, qres=%d", __FUNCTION__, __LINE__, qres);
						break;
					}
				}

#if 1
				if(pDanteSender) {
					BYTE * pFrameBuffer;
					ULONG nFrameBufferLen;
					qcap2_rcbuffer_to_buffer(pRCBuffer, &pFrameBuffer, &nFrameBufferLen);

					std::shared_ptr<qcap2_av_frame_t> pAVFrame((qcap2_av_frame_t*)qcap2_rcbuffer_lock_data(pRCBuffer),
						[pRCBuffer](qcap2_av_frame_t*) {
							qcap2_rcbuffer_unlock_data(pRCBuffer);
						}
					);

					double dSampleTime;
					qcap2_av_frame_get_sample_time(pAVFrame.get(), &dSampleTime);

					qres = QCAP_SET_AUDIO_BROADCAST_SERVER_UNCOMPRESSION_BUFFER(pDanteSender, 0, pFrameBuffer, nFrameBufferLen, dSampleTime);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): QCAP_SET_AUDIO_BROADCAST_SERVER_UNCOMPRESSION_BUFFER() failed, qres=%d", __FUNCTION__, __LINE__, qres);
						break;
					}
				}
#endif
			}

			return QCAP_RT_OK;
		}

		QRESULT StartAenc(free_stack_t& _FreeStack_, ULONG nAudioChannels, ULONG nAudioBitsPerSample,
				ULONG nAudioSampleFrequency, ULONG nAudioEncoderFormat, qcap2_audio_encoder_t** ppAenc, qcap2_event_t** ppEvent) {
			QRESULT qres = QCAP_RS_SUCCESSFUL;

			switch(1) { case 1:
				qcap2_event_t* pEvent;
				qres = NewEvent(_FreeStack_, &pEvent);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): NewEvent() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				qcap2_audio_encoder_t* pAenc = qcap2_audio_encoder_new();
				_FreeStack_ += [pAenc]() {
					qcap2_audio_encoder_delete(pAenc);
				};

				{
					std::shared_ptr<qcap2_audio_encoder_property_t> pAencProp(qcap2_audio_encoder_property_new(),
						qcap2_audio_encoder_property_delete);

					qcap2_audio_encoder_property_set_property(pAencProp.get(),
						QCAP_ENCODER_TYPE_SOFTWARE, nAudioEncoderFormat,
						nAudioChannels, nAudioBitsPerSample, nAudioSampleFrequency);
					qcap2_audio_encoder_set_audio_property(pAenc, pAencProp.get());
				}

				qcap2_audio_encoder_set_event(pAenc, pEvent);
				qcap2_audio_encoder_set_frame_count(pAenc, 16);
				qcap2_audio_encoder_set_packet_count(pAenc, 16);
				qcap2_audio_encoder_set_multithread(pAenc, true);

				qres = qcap2_audio_encoder_start(pAenc);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGD("%s(%d): qcap2_audio_encoder_start() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
				_FreeStack_ += [pAenc]() {
					QRESULT qres;

					qres = qcap2_audio_encoder_stop(pAenc);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGD("%s(%d): qcap2_audio_encoder_stop() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					}
				};

				*ppAenc = pAenc;
				*ppEvent = pEvent;
			}

			return qres;
		}

		QRETURN OnAenc(qcap2_audio_encoder_t* pAenc, qcap2_audio_decoder_t* pAdec_rtsp) {
			QRESULT qres;

			switch(1) { case 1:
				qcap2_rcbuffer_t* pRCBuffer;
				qres = qcap2_audio_encoder_pop(pAenc, &pRCBuffer);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_audio_encoder_pop() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
				std::shared_ptr<qcap2_rcbuffer_t> pRCBuffer_(pRCBuffer,
					qcap2_rcbuffer_release);

				if(pAdec_rtsp) {
					qres = qcap2_audio_decoder_push(pAdec_rtsp, pRCBuffer);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): qcap2_audio_decoder_push() failed, qres=%d", __FUNCTION__, __LINE__, qres);
						break;
					}
				}
			}

			return QCAP_RT_OK;
		}

		QRESULT StartRTSPMuxer(free_stack_t& _FreeStack_, ULONG nColorSpaceType, ULONG nVideoWidth, ULONG nVideoHeight,
			BOOL bVideoIsInterleaved, double dVideoFrameRate, ULONG nEncoderFormat, ULONG nVideoBitRate,
			ULONG nAudioChannels, ULONG nAudioBitsPerSample, ULONG nAudioSampleFrequency, ULONG nAudioEncoderFormat,
			qcap2_muxer_t** ppMuxer, qcap2_video_decoder_t** ppVdec, qcap2_audio_decoder_t** ppAdec) {
			QRESULT qres = QCAP_RS_SUCCESSFUL;

			switch(1) { case 1:
				qcap2_muxer_t* pMuxer = qcap2_muxer_new();
				_FreeStack_ += [pMuxer]() {
					qcap2_muxer_delete(pMuxer);
				};

				qcap2_muxer_set_type(pMuxer, QCAP2_MUXER_TYPE_RTSP);
				qcap2_muxer_set_endpoint(pMuxer, "0.0.0.0", 554);
				qcap2_muxer_set_max_threads(pMuxer, 4);
				qcap2_muxer_set_realm(pMuxer, "YUAN");
				qcap2_muxer_add_user(pMuxer, "root", "root");
				qcap2_muxer_add_user(pMuxer, "guest", "guest");

				{
					std::shared_ptr<qcap2_program_info_t> pProgInfo(
						qcap2_program_info_new(), qcap2_program_info_delete);

					qcap2_program_info_set_metadata(pProgInfo.get(), "cname", "YUAN");
					qcap2_program_info_set_metadata(pProgInfo.get(), "resource_name", "session0");
					qcap2_program_info_set_video_decoder_count(pProgInfo.get(), 1);
					qcap2_program_info_set_video_decoder_index(pProgInfo.get(), 0, 0);
					qcap2_program_info_set_audio_decoder_count(pProgInfo.get(), 1);
					qcap2_program_info_set_audio_decoder_index(pProgInfo.get(), 0, 0);
					qcap2_muxer_add_program_info(pMuxer, pProgInfo.get());
				}

				qres = qcap2_muxer_start(pMuxer);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_muxer_start() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
				_FreeStack_ += [pMuxer]() {
					QRESULT qres;

					qres = qcap2_muxer_stop(pMuxer);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): qcap2_muxer_stop() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					}
				};

				qcap2_video_decoder_t* pVdec = qcap2_muxer_get_video_decoder(pMuxer, 0);
				{
					std::shared_ptr<qcap2_video_encoder_property_t> pVencProp(
						qcap2_video_encoder_property_new(), qcap2_video_encoder_property_delete);

					UINT nGpuNum = 0;
					ULONG nEncoderType = QCAP_ENCODER_TYPE_SOFTWARE;
					// ULONG nEncoderFormat = QCAP_ENCODER_FORMAT_H264;
					// ULONG nColorSpaceType = XX;
					ULONG nWidth = nVideoWidth;
					ULONG nHeight = nVideoHeight;
					double dFrameRate = dVideoFrameRate;
					ULONG nRecordProfile = QCAP_RECORD_PROFILE_HIGH;
					ULONG nRecordLevel = QCAP_RECORD_LEVEL_51;
					ULONG nRecordEntropy = QCAP_RECORD_ENTROPY_CABAC;
					ULONG nRecordComplexity = QCAP_RECORD_COMPLEXITY_0;
					ULONG nRecordMode = QCAP_RECORD_MODE_CBR;
					ULONG nQuality = 8000;
					ULONG nBitRate = nVideoBitRate;
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
					qcap2_video_decoder_set_video_property(pVdec, pVencProp.get());
				}
				// qcap2_video_decoder_set_payload_type(pVdec, 98);

				qcap2_audio_decoder_t* pAdec = qcap2_muxer_get_audio_decoder(pMuxer, 0);
				{
					std::shared_ptr<qcap2_audio_encoder_property_t> pAencProp(qcap2_audio_encoder_property_new(),
						qcap2_audio_encoder_property_delete);

					qcap2_audio_encoder_property_set_property(pAencProp.get(),
						QCAP_ENCODER_TYPE_SOFTWARE, nAudioEncoderFormat,
						nAudioChannels, nAudioBitsPerSample, nAudioSampleFrequency);
					qcap2_audio_decoder_set_audio_property(pAdec, pAencProp.get());
				}
				// qcap2_audio_decoder_set_payload_type(pAdec, 96);

				qres = qcap2_muxer_play(pMuxer);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_muxer_play() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				qres = qcap2_video_decoder_start(pVdec);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_video_decoder_start() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
				_FreeStack_ += [pVdec]() {
					QRESULT qres;

					qres = qcap2_video_decoder_stop(pVdec);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): qcap2_video_decoder_stop() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					}
				};

				qres = qcap2_audio_decoder_start(pAdec);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_audio_decoder_start() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
				_FreeStack_ += [pAdec]() {
					QRESULT qres;

					qres = qcap2_audio_decoder_stop(pAdec);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): qcap2_audio_decoder_stop() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					}
				};

				*ppMuxer = pMuxer;
				*ppVdec = pVdec;
				*ppAdec = pAdec;
			}

			return qres;
		}

		QRESULT StartDanteServer(free_stack_t& _FreeStack_, ULONG nColorSpaceType, ULONG nVideoWidth, ULONG nVideoHeight,
			BOOL bVideoIsInterleaved, double dVideoFrameRate, ULONG nEncoderFormat, ULONG nVideoBitRate,
			ULONG nAudioChannels, ULONG nAudioBitsPerSample, ULONG nAudioSampleFrequency, ULONG nAudioEncoderFormat,
			PVOID* ppDanteServer, PVOID* ppSender) {
			QRESULT qres = QCAP_RS_SUCCESSFUL;

			switch(1) { case 1:
				PVOID pDanteServer;
				qres = QCAP_CREATE_DANTE_SERVER(&pDanteServer);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): QCAP_CREATE_DANTE_SERVER() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
				_FreeStack_ += [pDanteServer]() {
					QRESULT qres;

					qres = QCAP_DESTROY_DANTE_SERVER(pDanteServer);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): QCAP_DESTROY_DANTE_SERVER() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					}
				};

				qres = QCAP_SET_DANTE_SERVER_CUSTOM_PROPERTY(pDanteServer, 1, 0);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): QCAP_SET_DANTE_SERVER_CUSTOM_PROPERTY() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				qres = QCAP_REGISTER_DANTE_SERVER_MESSAGE_CALLBACK(pDanteServer, &self_t::_DANTE_SERVER_MESSAGE_CALLBACK, this);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): QCAP_SET_DANTE_SERVER_CUSTOM_PROPERTY() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				qres = QCAP_REGISTER_DANTE_SERVER_TX_AUDIO_REQUEST_CALLBACK(pDanteServer, &self_t::_DANTE_SERVER_TX_AUDIO_REQUEST_CALLBACK, this);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): QCAP_SET_DANTE_SERVER_CUSTOM_PROPERTY() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				qres = QCAP_REGISTER_DANTE_SERVER_NO_RESPOND_CALLBACK(pDanteServer, &self_t::_DANTE_SERVER_NO_RESPOND_CALLBACK, this);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): QCAP_SET_DANTE_SERVER_CUSTOM_PROPERTY() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				qres = QCAP_START_DANTE_SERVER(pDanteServer);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): QCAP_START_DANTE_SERVER() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
				_FreeStack_ += [pDanteServer]() {
					QRESULT qres;

					qres = QCAP_STOP_DANTE_SERVER(pDanteServer);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): QCAP_STOP_DANTE_SERVER() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					}
				};

				PVOID pSender;
				qres = QCAP_CREATE_DANTE_SENDER(pDanteServer, 0, &pSender, 0, 0);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): QCAP_CREATE_DANTE_SENDER() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
				_FreeStack_ += [pSender]() {
					QRESULT qres;

					qres = QCAP_DESTROY_BROADCAST_SERVER(pSender);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): QCAP_DESTROY_BROADCAST_SERVER() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					}
				};

				qres = QCAP_SET_VIDEO_BROADCAST_SERVER_PROPERTY(pSender, 0,
					QCAP_ENCODER_TYPE_SOFTWARE, nEncoderFormat, nColorSpaceType,
					nVideoWidth, nVideoHeight, dVideoFrameRate, QCAP_RECORD_MODE_CBR, 6000,
					nVideoBitRate, 30, 0, 0, NULL, FALSE, FALSE, QCAP_BROADCAST_FLAG_NETWORK);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): QCAP_SET_VIDEO_BROADCAST_SERVER_PROPERTY() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				qres = QCAP_SET_AUDIO_BROADCAST_SERVER_PROPERTY(pSender, 0,
					QCAP_ENCODER_TYPE_SOFTWARE, QCAP_ENCODER_FORMAT_PCM, nAudioChannels, 32, nAudioSampleFrequency);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): QCAP_SET_AUDIO_BROADCAST_SERVER_PROPERTY_EX() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				qres = QCAP_START_BROADCAST_SERVER(pSender);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): QCAP_START_BROADCAST_SERVER() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
				_FreeStack_ += [pSender]() {
					QRESULT qres;

					qres = QCAP_STOP_BROADCAST_SERVER(pSender);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): QCAP_STOP_BROADCAST_SERVER() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					}
				};

				*ppDanteServer = pDanteServer;
				*ppSender = pSender;
			}

			return qres;
		}

		static QRETURN QCAP_EXPORT _DANTE_SERVER_MESSAGE_CALLBACK(PVOID pServer, UINT nVideoChannel, DWORD dwFlags, PVOID pUserData) {
			self_t* pThis = (self_t*)pUserData;

			return pThis->OnDanteMessage(nVideoChannel, dwFlags);
		}

		QRETURN OnDanteMessage(UINT nVideoChannel, DWORD dwFlags) {
			return QCAP_RT_OK;
		}

		static QRETURN QCAP_EXPORT _DANTE_SERVER_TX_AUDIO_REQUEST_CALLBACK(PVOID pServer, double dSampleTime, PVOID pUserData) {
			self_t* pThis = (self_t*)pUserData;

			return pThis->OnDanteTxAudioRequest(dSampleTime);
		}

		QRETURN OnDanteTxAudioRequest(double dSampleTime) {
			return QCAP_RT_OK;
		}

		static QRETURN QCAP_EXPORT _DANTE_SERVER_NO_RESPOND_CALLBACK(PVOID pServer, BOOL bActivate, PVOID pUserData) {
			self_t* pThis = (self_t*)pUserData;

			return pThis->OnDanteServerNoResponse(bActivate);
		}

		QRETURN OnDanteServerNoResponse(BOOL bActivate) {
			return QCAP_RT_OK;
		}
	} mTestCase3;
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
