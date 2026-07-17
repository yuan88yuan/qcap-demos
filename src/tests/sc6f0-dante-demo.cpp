#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <atomic>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <unistd.h>
#include <linux/netlink.h>
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

#if 1
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

#if 0
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
