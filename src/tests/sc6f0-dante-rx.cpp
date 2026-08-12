#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <thread>
#include <vector>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <unistd.h>
#include <linux/netlink.h>
#include <alsa/asoundlib.h>
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

ZZ_INIT_LOG("sc6f0-dante-rx");

int g_argc = 0;
char** g_argv = NULL;

ZZ_MODULE_DECL(__zz_log__);

using namespace __zz_clock__;

namespace __sc6f0_dante_rx__ {
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
		if(! props || ! name) return 0;

		for (uint32_t i = 0; i < props->count_props; i++) {
			std::shared_ptr<drmModePropertyRes> p(drmModeGetProperty(fd, props->props[i]), drmModeFreeProperty);
			if(! p) {
				LOGW("drmModeGetProperty(%u) failed while looking up %s",
					props->props[i], name);
				continue;
			}
			if (!strcmp(p->name, name)) {
				if(prop_res) *prop_res = p;
				return p->prop_id;
			}
		}

		return 0;
	}
}

using namespace __sc6f0_dante_rx__;
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
		mTestCase1.DoWork();

		return 0;
	}

	struct TestCase1 : public TestCase {
		typedef TestCase1 self_t;
		typedef TestCase super_t;

		dau_service* pHdmiTxDauServ;
		int mDrmFd;
		uint32_t mConnectorId;
		uint32_t mCrtcId;
		uint32_t mPrimaryPlaneId;
		uint32_t mPrimaryFbId;
		uint32_t mPrimaryBoHandle;
		uint32_t mOverlayPlaneId;
		uint32_t mOverlayFbId;
		uint32_t mOverlayBoHandle;
		bool mLastConnectorConnected;
		bool mHaveLastConnectorState;
		struct video_format mCurrentVideoFormat;
		snd_pcm_t* mAlsaPcm;

		enum AudioMode {
			AUDIO_MODE_STREAM = 0,
			AUDIO_MODE_TEST_PATTERN = 1,
		};
		AudioMode mAudioMode;
		std::thread* mToneThread;
		std::atomic<bool> mToneRunning;

		static const char* video_colorformat_to_string(enum video_colorformat colorformat) {
			switch(colorformat) {
			case VIDEO_COLORFORMAT_YUV444: return "YUV444";
			case VIDEO_COLORFORMAT_YUV422: return "YUV422";
			case VIDEO_COLORFORMAT_YUV420: return "YUV420";
			case VIDEO_COLORFORMAT_RGB: return "RGB";
			default: return "unknown";
			}
		}

		static const char* drm_connection_to_string(drmModeConnection connection) {
			switch(connection) {
			case DRM_MODE_CONNECTED: return "connected";
			case DRM_MODE_DISCONNECTED: return "disconnected";
			case DRM_MODE_UNKNOWNCONNECTION: return "unknown";
			default: return "invalid";
			}
		}

		static void format_bytes(char* dst, size_t dstLen, const char* data, size_t len) {
			if(dstLen == 0) return;

			dst[0] = '\0';
			if(! data) return;

			size_t off = 0;
			for(size_t i = 0; i < len && off < dstLen; i++) {
				int n = snprintf(dst + off, dstLen - off, "%s%02x", i == 0 ? "" : " ",
					(unsigned int)(unsigned char)data[i]);
				if(n < 0) break;
				if((size_t)n >= dstLen - off) {
					off = dstLen - 1;
					break;
				}
				off += (size_t)n;
			}
		}

		static bool write_edid_blob(const void* data, size_t len) {
			static const char* path = "/tmp/sc6f0-dante-rx-edid.bin";

			if(! data || len == 0) {
				LOGW("Cannot write EDID blob: empty data");
				return false;
			}

			int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600);
			if(fd < 0) {
				LOGE("open(%s) failed, err=%d", path, errno);
				return false;
			}

			const unsigned char* bytes = (const unsigned char*)data;
			size_t written = 0;
			bool success = true;
			while(written < len) {
				ssize_t n = write(fd, bytes + written, len - written);
				if(n < 0) {
					if(errno == EINTR) continue;
					LOGE("write(%s) failed after %zu/%zu bytes, err=%d",
						path, written, len, errno);
					success = false;
					break;
				}
				if(n == 0) {
					LOGE("write(%s) returned zero after %zu/%zu bytes",
						path, written, len);
					success = false;
					break;
				}
				written += (size_t)n;
			}

			if(close(fd) < 0) {
				LOGE("close(%s) failed, err=%d", path, errno);
				success = false;
			}
			if(success) LOGI("Wrote EDID blob (%zu bytes) to %s", len, path);
			return success;
		}

		static bool is_valid_edid(const unsigned char* edid, size_t len) {
			static const unsigned char edidHeader[8] = {
				0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00
			};

			if(! edid || len < 128) return false;
			if(memcmp(edid, edidHeader, sizeof(edidHeader)) != 0) return false;

			size_t blocks = len / 128;
			if(blocks > EDID_LEN / 128) blocks = EDID_LEN / 128;
			if(blocks == 0) return false;

			for(size_t block = 0; block < blocks; block++) {
				unsigned int sum = 0;
				for(size_t i = 0; i < 128; i++) {
					sum += edid[block * 128 + i];
				}
				if((sum & 0xff) != 0) return false;
			}

			return true;
		}

		static void init_default_video_format(struct video_format* format) {
			memset(format, 0x00, sizeof(*format));
			format->width = 3840;
			format->height = 2160;
			format->framerate = 60;
			format->colorformat = VIDEO_COLORFORMAT_YUV444;
			format->bpp = 8;
			format->interlaced = false;
			format->locked = true;
		}

		static bool get_prop_value(drmModeObjectProperties *props, uint32_t propId, uint64_t* value) {
			if(! props || ! value || propId == 0) return false;

			for(uint32_t i = 0; i < props->count_props; i++) {
				if(props->props[i] == propId) {
					*value = props->prop_values[i];
					return true;
				}
			}

			return false;
		}

		static bool atomic_add_property_checked(drmModeAtomicReq* req,
			uint32_t objectId, uint32_t propId, const char* propName, uint64_t value)
		{
			if(! req || propId == 0) {
				LOGE("%s(%d): missing DRM property %s on object %u",
					__FUNCTION__, __LINE__, propName ? propName : "(null)", objectId);
				return false;
			}

			int err = drmModeAtomicAddProperty(req, objectId, propId, value);
			LOGD("drmModeAtomicAddProperty(%u, %u:%s, %llu) = %d",
				objectId, propId, propName ? propName : "", (unsigned long long)value, err);
			if(err < 0) {
				LOGE("%s(%d): drmModeAtomicAddProperty(%u, %s) failed, err=%d",
					__FUNCTION__, __LINE__, objectId, propName ? propName : "(null)", err);
				return false;
			}

			return true;
		}

		static int find_crtc_index(drmModeRes* resources, uint32_t crtcId) {
			if(! resources) return -1;

			for(int i = 0; i < resources->count_crtcs; i++) {
				if(resources->crtcs[i] == crtcId) return i;
			}

			return -1;
		}

		static QRESULT find_connector_crtc(int fd, drmModeRes* resources,
			drmModeConnector* connector, uint32_t* crtcId, int* crtcIndex)
		{
			if(! resources || ! connector || ! crtcId || ! crtcIndex) return QCAP_RS_ERROR_INVALID_PARAMETER;

			if(connector->encoder_id != 0) {
				std::shared_ptr<drmModeEncoder> encoder(
					drmModeGetEncoder(fd, connector->encoder_id), drmModeFreeEncoder);
				if(encoder && encoder->crtc_id != 0) {
					int idx = find_crtc_index(resources, encoder->crtc_id);
					if(idx >= 0) {
						*crtcId = encoder->crtc_id;
						*crtcIndex = idx;
						return QCAP_RS_SUCCESSFUL;
					}
				}
			}

			for(int i = 0; i < connector->count_encoders; i++) {
				std::shared_ptr<drmModeEncoder> encoder(
					drmModeGetEncoder(fd, connector->encoders[i]), drmModeFreeEncoder);
				if(! encoder) continue;

				for(int j = 0; j < resources->count_crtcs; j++) {
					if(encoder->possible_crtcs & (1 << j)) {
						*crtcId = resources->crtcs[j];
						*crtcIndex = j;
						return QCAP_RS_SUCCESSFUL;
					}
				}
			}

			LOGE("%s(%d): no usable CRTC found for connector %u",
				__FUNCTION__, __LINE__, connector->connector_id);
			return QCAP_RS_ERROR_GENERAL;
		}

		static QRESULT find_matching_mode(drmModeConnector* connector,
			const struct video_format* format, drmModeModeInfo* mode)
		{
			if(! connector || ! format || ! mode) return QCAP_RS_ERROR_INVALID_PARAMETER;

			for(int i = 0; i < connector->count_modes; i++) {
				drmModeModeInfo* candidate = &connector->modes[i];
				bool modeInterlaced = (candidate->flags & DRM_MODE_FLAG_INTERLACE) != 0;
				double refresh = mode_vrefresh(candidate);
				bool refreshMatched = (format->framerate == 0) ||
					(fabs(refresh - (double)format->framerate) < 0.5) ||
					(candidate->vrefresh == (int)format->framerate);

				if(candidate->hdisplay == (int)format->width &&
					candidate->vdisplay == (int)format->height &&
					modeInterlaced == format->interlaced &&
					refreshMatched) {
					*mode = *candidate;
					LOGD("selected mode [%s] %d x %d @ %d (%.2f), flags=0x%x",
						mode->name, mode->hdisplay, mode->vdisplay, mode->vrefresh,
						mode_vrefresh(mode), mode->flags);
					return QCAP_RS_SUCCESSFUL;
				}
			}

			LOGE("%s(%d): no connector mode matches %ux%u@%uHz interlaced=%s",
				__FUNCTION__, __LINE__, format->width, format->height, format->framerate,
				format->interlaced ? "true" : "false");
			return QCAP_RS_ERROR_INVALID_PARAMETER;
		}

		static bool plane_is_primary(int fd, drmModeObjectProperties* props) {
			std::shared_ptr<drmModePropertyRes> typeProp;
			uint32_t propId = get_prop_id(fd, props, &typeProp, "type");
			uint64_t value = 0;

			if(propId == 0 || ! typeProp || ! get_prop_value(props, propId, &value)) return false;

			for(int i = 0; i < typeProp->count_enums; i++) {
				if(typeProp->enums[i].value == value &&
					strcmp(typeProp->enums[i].name, "Primary") == 0) {
					return true;
				}
			}

			return false;
		}

		static bool plane_supports_format(drmModePlane* plane, uint32_t drmFormat) {
			if(! plane) return false;

			for(uint32_t i = 0; i < plane->count_formats; i++) {
				if(plane->formats[i] == drmFormat) return true;
			}

			return false;
		}

		static uint32_t drm_format_bpp(uint32_t drmFormat) {
			switch(drmFormat) {
			case DRM_FORMAT_YUYV:
			case DRM_FORMAT_UYVY:
				return 16;
			case DRM_FORMAT_BGR888:
			case DRM_FORMAT_RGB888:
			case DRM_FORMAT_VUY888:
			case DRM_FORMAT_YUV444:
				return 24;
			default:
				return 32;
			}
		}

		static const char* drm_format_to_string(uint32_t drmFormat) {
			switch(drmFormat) {
			case DRM_FORMAT_BGR888: return "BG24";
			case DRM_FORMAT_RGB888: return "RG24";
			case DRM_FORMAT_XRGB8888: return "XR24";
			case DRM_FORMAT_XBGR8888: return "XB24";
			case DRM_FORMAT_VUY888: return "VU24";
			case DRM_FORMAT_XYUV8888: return "XY24";
			case DRM_FORMAT_YUYV: return "YUYV";
			case DRM_FORMAT_UYVY: return "UYVY";
			default: return "unknown";
			}
		}

		static bool choose_primary_fb_format(drmModePlane* plane, uint32_t* drmFormat, uint32_t* dumbBpp)
		{
			/*
			 * modetest.txt shows the primary plane only advertises BG24.  The DAU input
			 * colorformat is therefore intentionally ignored here; modeset the primary
			 * plane with a BG24 dumb framebuffer for every requested video mode.
			 */
			if(! plane_supports_format(plane, DRM_FORMAT_BGR888)) return false;

			*drmFormat = DRM_FORMAT_BGR888;
			*dumbBpp = 24;
			return true;
		}

		static QRESULT find_primary_plane(int fd, int crtcIndex,
			uint32_t* planeId, std::shared_ptr<drmModePlane>* planeOut)
		{
			if(crtcIndex < 0 || ! planeId || ! planeOut) return QCAP_RS_ERROR_INVALID_PARAMETER;

			std::shared_ptr<drmModePlaneRes> planes(
				drmModeGetPlaneResources(fd), drmModeFreePlaneResources);
			if(! planes) {
				LOGE("%s(%d): drmModeGetPlaneResources() failed", __FUNCTION__, __LINE__);
				return QCAP_RS_ERROR_GENERAL;
			}

			for(uint32_t i = 0; i < planes->count_planes; i++) {
				std::shared_ptr<drmModePlane> plane(
					drmModeGetPlane(fd, planes->planes[i]), drmModeFreePlane);
				if(! plane) continue;
				if(! (plane->possible_crtcs & (1 << crtcIndex))) continue;

				std::shared_ptr<drmModeObjectProperties> props(
					drmModeObjectGetProperties(fd, plane->plane_id, DRM_MODE_OBJECT_PLANE),
					drmModeFreeObjectProperties);
				if(! props || ! plane_is_primary(fd, props.get())) continue;

				*planeId = plane->plane_id;
				*planeOut = plane;
				return QCAP_RS_SUCCESSFUL;
			}

			LOGE("%s(%d): no primary plane found for CRTC index %d",
				__FUNCTION__, __LINE__, crtcIndex);
			return QCAP_RS_ERROR_GENERAL;
		}

		static void destroy_dumb_fb(int fd, uint32_t fb, uint32_t bo) {
			int err;

			if(fd < 0) return;

			if(fb != 0) {
				err = drmModeRmFB(fd, fb);
				if(err < 0) {
					err = errno;
					LOGE("%s(%d): drmModeRmFB(%u) failed, err=%d", __FUNCTION__, __LINE__, fb, err);
				}
			}

			if(bo != 0) {
				struct drm_mode_destroy_dumb dreq = { 0 };
				dreq.handle = bo;
				err = drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
				if(err < 0) {
					err = errno;
					LOGE("%s(%d): drmIoctl(DRM_IOCTL_MODE_DESTROY_DUMB, %u) failed, err=%d",
						__FUNCTION__, __LINE__, bo, err);
				}
			}
		}

		void cleanup_primary_fb() {
			destroy_dumb_fb(mDrmFd, mPrimaryFbId, mPrimaryBoHandle);
			mPrimaryFbId = 0;
			mPrimaryBoHandle = 0;
		}

		void cleanup_overlay_fb() {
			destroy_dumb_fb(mDrmFd, mOverlayFbId, mOverlayBoHandle);
			mOverlayFbId = 0;
			mOverlayBoHandle = 0;
		}

		void cleanup_drm_fbs() {
			cleanup_overlay_fb();
			cleanup_primary_fb();
		}

#if 0
		static QRESULT fill_yuyv_dumb_fb(int fd, uint32_t bo, uint32_t width, uint32_t height, uint32_t pitch) {
			QRESULT qres = QCAP_RS_SUCCESSFUL;
			int err;
			void* map = MAP_FAILED;

			switch(1) { case 1:
				if(fd < 0 || bo == 0 || width == 0 || height == 0 || pitch == 0) {
					qres = QCAP_RS_ERROR_INVALID_PARAMETER;
					LOGE("%s(%d): invalid YUYV dumb FB parameters", __FUNCTION__, __LINE__);
					break;
				}

				struct drm_mode_map_dumb mreq = { 0 };
				mreq.handle = bo;
				err = drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq);
				if(err < 0) {
					err = errno;
					qres = QCAP_RS_ERROR_GENERAL;
					LOGE("%s(%d): drmIoctl(DRM_IOCTL_MODE_MAP_DUMB, %u) failed, err=%d",
						__FUNCTION__, __LINE__, bo, err);
					break;
				}

				const size_t mapLen = (size_t)pitch * height;
				map = mmap(NULL, mapLen, PROT_READ | PROT_WRITE, MAP_SHARED, fd, mreq.offset);
				if(map == MAP_FAILED) {
					err = errno;
					qres = QCAP_RS_ERROR_GENERAL;
					LOGE("%s(%d): mmap(YUYV dumb FB) failed, err=%d", __FUNCTION__, __LINE__, err);
					break;
				}

				for(uint32_t y = 0; y < height; y++) {
					uint8_t* row = (uint8_t*)map + (size_t)y * pitch;
					for(uint32_t x = 0; x + 1 < width; x += 2) {
						row[x * 2 + 0] = 0x10; // Y0: limited-range black
						row[x * 2 + 1] = 0x80; // U
						row[x * 2 + 2] = 0x10; // Y1
						row[x * 2 + 3] = 0x80; // V
					}
				}
			}

			if(map != MAP_FAILED) {
				munmap(map, (size_t)pitch * height);
			}

			return qres;
		}
#endif

		QRESULT apply_drm_video_format(const struct video_format* format) {
			QRESULT qres = QCAP_RS_SUCCESSFUL;
			int err;
			uint32_t modeBlobId = 0;
			uint32_t fb = 0;
			uint32_t bo = 0;
			uint32_t overlayFb = 0;
			uint32_t overlayBo = 0;

			switch(1) { case 1:
				if(! format) {
					qres = QCAP_RS_ERROR_INVALID_PARAMETER;
					LOGE("%s(%d): video_format is NULL", __FUNCTION__, __LINE__);
					break;
				}
				if(mDrmFd < 0 || mConnectorId == 0) {
					qres = QCAP_RS_ERROR_GENERAL;
					LOGE("%s(%d): DRM is not initialized (fd=%d, connector=%u)",
						__FUNCTION__, __LINE__, mDrmFd, mConnectorId);
					break;
				}

				err = drmSetClientCap(mDrmFd, DRM_CLIENT_CAP_ATOMIC, 1);
				if(err) {
					err = errno;
					qres = QCAP_RS_ERROR_GENERAL;
					LOGE("%s(%d): drmSetClientCap(DRM_CLIENT_CAP_ATOMIC) failed, err=%d",
						__FUNCTION__, __LINE__, err);
					break;
				}

				err = drmSetClientCap(mDrmFd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
				if(err) {
					err = errno;
					qres = QCAP_RS_ERROR_GENERAL;
					LOGE("%s(%d): drmSetClientCap(DRM_CLIENT_CAP_UNIVERSAL_PLANES) failed, err=%d",
						__FUNCTION__, __LINE__, err);
					break;
				}

				std::shared_ptr<drmModeRes> resources(drmModeGetResources(mDrmFd), drmModeFreeResources);
				if(! resources) {
					qres = QCAP_RS_ERROR_GENERAL;
					LOGE("%s(%d): drmModeGetResources() failed", __FUNCTION__, __LINE__);
					break;
				}

				std::shared_ptr<drmModeConnector> connector(
					drmModeGetConnector(mDrmFd, mConnectorId), drmModeFreeConnector);
				if(! connector) {
					qres = QCAP_RS_ERROR_GENERAL;
					LOGE("%s(%d): drmModeGetConnector(%u) failed", __FUNCTION__, __LINE__, mConnectorId);
					break;
				}
				if(connector->connection != DRM_MODE_CONNECTED) {
					qres = QCAP_RS_ERROR_CONNECT_FAIL;
					LOGE("%s(%d): connector %u is not connected (connection=%d)",
						__FUNCTION__, __LINE__, mConnectorId, connector->connection);
					break;
				}

				drmModeModeInfo mode;
				qres = find_matching_mode(connector.get(), format, &mode);
				if(qres != QCAP_RS_SUCCESSFUL) break;

				uint32_t crtcId = 0;
				int crtcIndex = -1;
				qres = find_connector_crtc(mDrmFd, resources.get(), connector.get(), &crtcId, &crtcIndex);
				if(qres != QCAP_RS_SUCCESSFUL) break;

				uint32_t primaryPlaneId = 0;
				std::shared_ptr<drmModePlane> primaryPlane;
				qres = find_primary_plane(mDrmFd, crtcIndex, &primaryPlaneId, &primaryPlane);
				if(qres != QCAP_RS_SUCCESSFUL) break;

				const uint32_t overlayPlaneId = 34;
				std::shared_ptr<drmModePlane> overlayPlane(
					drmModeGetPlane(mDrmFd, overlayPlaneId), drmModeFreePlane);
				if(! overlayPlane) {
					qres = QCAP_RS_ERROR_GENERAL;
					LOGE("%s(%d): drmModeGetPlane(%u) failed", __FUNCTION__, __LINE__, overlayPlaneId);
					break;
				}
				if(! (overlayPlane->possible_crtcs & (1 << crtcIndex))) {
					qres = QCAP_RS_ERROR_INVALID_PARAMETER;
					LOGE("%s(%d): overlay plane %u cannot be used with CRTC index %d",
						__FUNCTION__, __LINE__, overlayPlaneId, crtcIndex);
					break;
				}

				uint32_t overlay_drm_format;
				switch(format->colorformat) {
				case VIDEO_COLORFORMAT_RGB:
					overlay_drm_format = DRM_FORMAT_BGR888;
					break;
				case VIDEO_COLORFORMAT_YUV444:
					overlay_drm_format = DRM_FORMAT_YUYV;
					break;
				default:
					qres = QCAP_RS_ERROR_INVALID_PARAMETER;
					LOGE("%s(%d): unexpected value, format->colorformat=%d (%s)",
						__FUNCTION__, __LINE__, format->colorformat, video_colorformat_to_string(format->colorformat));
					break;
				}

				if(! plane_supports_format(overlayPlane.get(), overlay_drm_format)) {
					qres = QCAP_RS_ERROR_INVALID_PARAMETER;
					LOGE("%s(%d): overlay plane %u does not support %s",
						__FUNCTION__, __LINE__, overlayPlaneId, drm_format_to_string(overlay_drm_format));
					break;
				}

				uint32_t drmFormat = 0;
				uint32_t dumbBpp = 0;
				if(! choose_primary_fb_format(primaryPlane.get(), &drmFormat, &dumbBpp)) {
					qres = QCAP_RS_ERROR_INVALID_PARAMETER;
					LOGE("%s(%d): primary plane %u has no supported framebuffer format for DRM_FORMAT_BGR888",
						__FUNCTION__, __LINE__, primaryPlaneId);
					break;
				}
				LOGI("using primary plane %u, CRTC %u, DRM fb format %s, dumb bpp=%u",
					primaryPlaneId, crtcId, drm_format_to_string(drmFormat), dumbBpp);

				struct drm_mode_create_dumb creq = { 0 };
				creq.width = format->width;
				creq.height = format->height;
				creq.bpp = dumbBpp;
				err = drmIoctl(mDrmFd, DRM_IOCTL_MODE_CREATE_DUMB, &creq);
				if(err < 0) {
					err = errno;
					qres = QCAP_RS_ERROR_GENERAL;
					LOGE("%s(%d): drmIoctl(DRM_IOCTL_MODE_CREATE_DUMB) failed, err=%d",
						__FUNCTION__, __LINE__, err);
					break;
				}
				bo = creq.handle;

				uint32_t handles[4] = { creq.handle, 0, 0, 0 };
				uint32_t pitches[4] = { creq.pitch, 0, 0, 0 };
				uint32_t offsets[4] = { 0, 0, 0, 0 };
				err = drmModeAddFB2(mDrmFd, creq.width, creq.height, drmFormat,
					handles, pitches, offsets, &fb, 0);
				if(err < 0) {
					err = errno;
					qres = QCAP_RS_ERROR_GENERAL;
					LOGE("%s(%d): drmModeAddFB2(%ux%u, %s) failed, err=%d",
						__FUNCTION__, __LINE__, creq.width, creq.height,
						drm_format_to_string(drmFormat), err);
					break;
				}

				{
					const uint32_t overlayDrmFormat = overlay_drm_format;
					const uint32_t overlayBpp = drm_format_bpp(overlayDrmFormat);

					struct drm_mode_create_dumb overlayCreq = { 0 };
					overlayCreq.width = format->width;
					overlayCreq.height = format->height;
					overlayCreq.bpp = overlayBpp;
					err = drmIoctl(mDrmFd, DRM_IOCTL_MODE_CREATE_DUMB, &overlayCreq);
					if(err < 0) {
						err = errno;
						qres = QCAP_RS_ERROR_GENERAL;
						LOGE("%s(%d): drmIoctl(DRM_IOCTL_MODE_CREATE_DUMB, overlay %s) failed, err=%d",
							__FUNCTION__, __LINE__, drm_format_to_string(overlayDrmFormat), err);
						break;
					}
					overlayBo = overlayCreq.handle;

#if 0
					qres = fill_yuyv_dumb_fb(mDrmFd, overlayBo, overlayCreq.width,
						overlayCreq.height, overlayCreq.pitch);
					if(qres != QCAP_RS_SUCCESSFUL) break;
#endif

					uint32_t overlayHandles[4] = { overlayCreq.handle, 0, 0, 0 };
					uint32_t overlayPitches[4] = { overlayCreq.pitch, 0, 0, 0 };
					uint32_t overlayOffsets[4] = { 0, 0, 0, 0 };
					err = drmModeAddFB2(mDrmFd, overlayCreq.width, overlayCreq.height,
						overlayDrmFormat, overlayHandles, overlayPitches, overlayOffsets,
						&overlayFb, 0);
					if(err < 0) {
						err = errno;
						qres = QCAP_RS_ERROR_GENERAL;
						LOGE("%s(%d): drmModeAddFB2(%ux%u, overlay %s) failed, err=%d",
							__FUNCTION__, __LINE__, overlayCreq.width, overlayCreq.height,
							drm_format_to_string(overlayDrmFormat), err);
						break;
					}
					LOGI("using overlay plane %u, DRM fb format %s, dumb bpp=%u",
						overlayPlaneId, drm_format_to_string(overlayDrmFormat), overlayBpp);
				}
				if(qres != QCAP_RS_SUCCESSFUL) break;

				err = drmModeCreatePropertyBlob(mDrmFd, &mode, sizeof(mode), &modeBlobId);
				if(err) {
					err = errno;
					qres = QCAP_RS_ERROR_GENERAL;
					LOGE("%s(%d): drmModeCreatePropertyBlob() failed, err=%d", __FUNCTION__, __LINE__, err);
					break;
				}

				std::shared_ptr<drmModeObjectProperties> propsCrtc(
					drmModeObjectGetProperties(mDrmFd, crtcId, DRM_MODE_OBJECT_CRTC),
					drmModeFreeObjectProperties);
				std::shared_ptr<drmModeObjectProperties> propsConn(
					drmModeObjectGetProperties(mDrmFd, mConnectorId, DRM_MODE_OBJECT_CONNECTOR),
					drmModeFreeObjectProperties);
				std::shared_ptr<drmModeObjectProperties> propsPlane(
					drmModeObjectGetProperties(mDrmFd, primaryPlaneId, DRM_MODE_OBJECT_PLANE),
					drmModeFreeObjectProperties);
				std::shared_ptr<drmModeObjectProperties> propsOverlay(
					drmModeObjectGetProperties(mDrmFd, overlayPlaneId, DRM_MODE_OBJECT_PLANE),
					drmModeFreeObjectProperties);
				if(! propsCrtc || ! propsConn || ! propsPlane || ! propsOverlay) {
					qres = QCAP_RS_ERROR_GENERAL;
					LOGE("%s(%d): failed to get DRM object properties", __FUNCTION__, __LINE__);
					break;
				}

				uint32_t propCrtcModeId = get_prop_id(mDrmFd, propsCrtc.get(), NULL, "MODE_ID");
				uint32_t propCrtcActive = get_prop_id(mDrmFd, propsCrtc.get(), NULL, "ACTIVE");
				uint32_t propConnCrtcId = get_prop_id(mDrmFd, propsConn.get(), NULL, "CRTC_ID");
				uint32_t propPlaneCrtcId = get_prop_id(mDrmFd, propsPlane.get(), NULL, "CRTC_ID");
				uint32_t propPlaneFbId = get_prop_id(mDrmFd, propsPlane.get(), NULL, "FB_ID");
				uint32_t propPlaneCrtcX = get_prop_id(mDrmFd, propsPlane.get(), NULL, "CRTC_X");
				uint32_t propPlaneCrtcY = get_prop_id(mDrmFd, propsPlane.get(), NULL, "CRTC_Y");
				uint32_t propPlaneCrtcW = get_prop_id(mDrmFd, propsPlane.get(), NULL, "CRTC_W");
				uint32_t propPlaneCrtcH = get_prop_id(mDrmFd, propsPlane.get(), NULL, "CRTC_H");
				uint32_t propPlaneSrcX = get_prop_id(mDrmFd, propsPlane.get(), NULL, "SRC_X");
				uint32_t propPlaneSrcY = get_prop_id(mDrmFd, propsPlane.get(), NULL, "SRC_Y");
				uint32_t propPlaneSrcW = get_prop_id(mDrmFd, propsPlane.get(), NULL, "SRC_W");
				uint32_t propPlaneSrcH = get_prop_id(mDrmFd, propsPlane.get(), NULL, "SRC_H");
				uint32_t propOverlayCrtcId = get_prop_id(mDrmFd, propsOverlay.get(), NULL, "CRTC_ID");
				uint32_t propOverlayFbId = get_prop_id(mDrmFd, propsOverlay.get(), NULL, "FB_ID");
				uint32_t propOverlayCrtcX = get_prop_id(mDrmFd, propsOverlay.get(), NULL, "CRTC_X");
				uint32_t propOverlayCrtcY = get_prop_id(mDrmFd, propsOverlay.get(), NULL, "CRTC_Y");
				uint32_t propOverlayCrtcW = get_prop_id(mDrmFd, propsOverlay.get(), NULL, "CRTC_W");
				uint32_t propOverlayCrtcH = get_prop_id(mDrmFd, propsOverlay.get(), NULL, "CRTC_H");
				uint32_t propOverlaySrcX = get_prop_id(mDrmFd, propsOverlay.get(), NULL, "SRC_X");
				uint32_t propOverlaySrcY = get_prop_id(mDrmFd, propsOverlay.get(), NULL, "SRC_Y");
				uint32_t propOverlaySrcW = get_prop_id(mDrmFd, propsOverlay.get(), NULL, "SRC_W");
				uint32_t propOverlaySrcH = get_prop_id(mDrmFd, propsOverlay.get(), NULL, "SRC_H");

				std::shared_ptr<drmModeAtomicReq> req(drmModeAtomicAlloc(), drmModeAtomicFree);
				if(! req) {
					qres = QCAP_RS_ERROR_GENERAL;
					LOGE("%s(%d): drmModeAtomicAlloc() failed", __FUNCTION__, __LINE__);
					break;
				}

				bool addOk = true;
				addOk &= atomic_add_property_checked(req.get(), mConnectorId, propConnCrtcId, "CRTC_ID", crtcId);
				addOk &= atomic_add_property_checked(req.get(), crtcId, propCrtcModeId, "MODE_ID", modeBlobId);
				addOk &= atomic_add_property_checked(req.get(), crtcId, propCrtcActive, "ACTIVE", 1);
				addOk &= atomic_add_property_checked(req.get(), primaryPlaneId, propPlaneCrtcId, "CRTC_ID", crtcId);
				addOk &= atomic_add_property_checked(req.get(), primaryPlaneId, propPlaneFbId, "FB_ID", fb);
				addOk &= atomic_add_property_checked(req.get(), primaryPlaneId, propPlaneCrtcX, "CRTC_X", 0);
				addOk &= atomic_add_property_checked(req.get(), primaryPlaneId, propPlaneCrtcY, "CRTC_Y", 0);
				addOk &= atomic_add_property_checked(req.get(), primaryPlaneId, propPlaneCrtcW, "CRTC_W", format->width);
				addOk &= atomic_add_property_checked(req.get(), primaryPlaneId, propPlaneCrtcH, "CRTC_H", format->height);
				addOk &= atomic_add_property_checked(req.get(), primaryPlaneId, propPlaneSrcX, "SRC_X", 0);
				addOk &= atomic_add_property_checked(req.get(), primaryPlaneId, propPlaneSrcY, "SRC_Y", 0);
				addOk &= atomic_add_property_checked(req.get(), primaryPlaneId, propPlaneSrcW, "SRC_W", (uint64_t)format->width << 16);
				addOk &= atomic_add_property_checked(req.get(), primaryPlaneId, propPlaneSrcH, "SRC_H", (uint64_t)format->height << 16);
				addOk &= atomic_add_property_checked(req.get(), overlayPlaneId, propOverlayCrtcId, "CRTC_ID", crtcId);
				addOk &= atomic_add_property_checked(req.get(), overlayPlaneId, propOverlayFbId, "FB_ID", overlayFb);
				addOk &= atomic_add_property_checked(req.get(), overlayPlaneId, propOverlayCrtcX, "CRTC_X", 0);
				addOk &= atomic_add_property_checked(req.get(), overlayPlaneId, propOverlayCrtcY, "CRTC_Y", 0);
				addOk &= atomic_add_property_checked(req.get(), overlayPlaneId, propOverlayCrtcW, "CRTC_W", format->width);
				addOk &= atomic_add_property_checked(req.get(), overlayPlaneId, propOverlayCrtcH, "CRTC_H", format->height);
				addOk &= atomic_add_property_checked(req.get(), overlayPlaneId, propOverlaySrcX, "SRC_X", 0);
				addOk &= atomic_add_property_checked(req.get(), overlayPlaneId, propOverlaySrcY, "SRC_Y", 0);
				addOk &= atomic_add_property_checked(req.get(), overlayPlaneId, propOverlaySrcW, "SRC_W", (uint64_t)format->width << 16);
				addOk &= atomic_add_property_checked(req.get(), overlayPlaneId, propOverlaySrcH, "SRC_H", (uint64_t)format->height << 16);
				if(! addOk) {
					qres = QCAP_RS_ERROR_GENERAL;
					break;
				}

				err = drmModeAtomicCommit(mDrmFd, req.get(), DRM_MODE_ATOMIC_ALLOW_MODESET, NULL);
				if(err < 0) {
					err = errno;
					qres = QCAP_RS_ERROR_GENERAL;
					LOGE("%s(%d): drmModeAtomicCommit(DRM_MODE_ATOMIC_ALLOW_MODESET) failed, err=%d",
						__FUNCTION__, __LINE__, err);
					break;
				}

				uint32_t oldFb = mPrimaryFbId;
				uint32_t oldBo = mPrimaryBoHandle;
				uint32_t oldOverlayFb = mOverlayFbId;
				uint32_t oldOverlayBo = mOverlayBoHandle;
				mPrimaryFbId = fb;
				mPrimaryBoHandle = bo;
				mOverlayPlaneId = overlayPlaneId;
				mOverlayFbId = overlayFb;
				mOverlayBoHandle = overlayBo;
				mCrtcId = crtcId;
				mPrimaryPlaneId = primaryPlaneId;
				mCurrentVideoFormat = *format;
				mCurrentVideoFormat.locked = true;
				fb = 0;
				bo = 0;
				overlayFb = 0;
				overlayBo = 0;
				destroy_dumb_fb(mDrmFd, oldFb, oldBo);
				destroy_dumb_fb(mDrmFd, oldOverlayFb, oldOverlayBo);

				LOGI("DRM modeset complete: connector=%u crtc=%u primary_plane=%u mode=%ux%u@%uHz",
					mConnectorId, mCrtcId, mPrimaryPlaneId,
					format->width, format->height, format->framerate);
			}

			if(modeBlobId != 0) {
				err = drmModeDestroyPropertyBlob(mDrmFd, modeBlobId);
				if(err) {
					err = errno;
					LOGE("%s(%d): drmModeDestroyPropertyBlob(%u) failed, err=%d",
						__FUNCTION__, __LINE__, modeBlobId, err);
				}
			}
			destroy_dumb_fb(mDrmFd, fb, bo);
			destroy_dumb_fb(mDrmFd, overlayFb, overlayBo);

			return qres;
		}

		bool query_connector_connected(bool& connected) {
			if(mDrmFd < 0 || mConnectorId == 0) return false;

			std::shared_ptr<drmModeConnector> pConnector(
				drmModeGetConnector(mDrmFd, mConnectorId),
				drmModeFreeConnector);
			if(! pConnector) return false;

			connected = (pConnector->connection == DRM_MODE_CONNECTED);
			LOGD("connector %u connection=%d (%s)", mConnectorId,
				pConnector->connection, drm_connection_to_string(pConnector->connection));

			return true;
		}

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

			bool connected = false;

			if(! query_connector_connected(connected)) {
				LOGW("failed to query DRM connector state (fd=%d, connector=%u)", mDrmFd, mConnectorId);
			}

			dau_reply_get_cable_status(dau, message, connected);
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

			dau_reply_get_video_format(dau, message, &mCurrentVideoFormat);
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

			struct audio_info info = {
				.channel_count = 2,
				.speaker_map = AUDIO_SM_FR_FL,
				.level_shift_value = 0,
				.downmix_inhibit = false,
			};

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
			self_t* pThis = (self_t*)ctxt;

			pThis->set_video_format(dau, message, format);
		}

		void set_video_format(struct dau_service *dau,
						struct DBusMessage *message,
						const struct video_format *format)
		{
			LOGI("---tag---: %s", __FUNCTION__);

			if(format) {
				LOGI("video_format: %ux%u@%uHz, colorformat=%d (%s), bpp=%u, interlaced=%s, locked=%s",
					format->width, format->height, format->framerate,
					(int)format->colorformat, video_colorformat_to_string(format->colorformat),
					format->bpp,
					format->interlaced ? "true" : "false",
					format->locked ? "true" : "false");
			} else {
				LOGW("video_format is NULL");
			}

			struct video_format new_video_format = *format;

#if 0
			new_video_format.colorformat = VIDEO_COLORFORMAT_RGB;
			LOGW("DEBUG: new_video_format.colorformat=%d (%s)", (int)new_video_format.colorformat, video_colorformat_to_string(new_video_format.colorformat));
#endif

			QRESULT qres = apply_drm_video_format(&new_video_format);
			if(qres != QCAP_RS_SUCCESSFUL) {
				LOGE("%s(%d): apply_drm_video_format() failed, qres=%d", __FUNCTION__, __LINE__, qres);
				dau_reply_error(dau, message, "Failed to apply DRM video format");
				return;
			}

			dau_reply_success(dau, message);
		}

		static void _set_audio_info(struct dau_service *dau,
						   struct DBusMessage *message,
						   const struct audio_info *info,
						   void *ctxt)
		{
			self_t* pThis = (self_t*)ctxt;

			LOGI("---tag---: %s", __FUNCTION__);
			if(! pThis->set_audio_info(info)) {
				dau_reply_error(dau, message, "Failed to configure ALSA audio output");
				return;
			}

			dau_reply_success(dau, message);
		}

		static AudioMode audio_mode_from_env() {
			/*
			 * SC6F0_DANTE_RX_AUDIO_MODE selects the audio source:
			 *   stream      (default) - fixed FPGA module, switch port_enable=0,1
			 *   testpattern           - xilinx audio formatter IP reading PS DDR
			 *                           data written via ALSA, port_enable=0,0
			 */
			const char* strVal = getenv("SC6F0_DANTE_RX_AUDIO_MODE");
			if(strVal) {
				if(strcmp(strVal, "stream") == 0) return AUDIO_MODE_STREAM;
				if(strcmp(strVal, "testpattern") == 0) return AUDIO_MODE_TEST_PATTERN;
				LOGW("SC6F0_DANTE_RX_AUDIO_MODE=%s is unknown; using default (stream)", strVal);
			}
			return AUDIO_MODE_STREAM;
		}

		static bool write_sysfs_value(const char* path, const char* value) {
			int fd = open(path, O_WRONLY | O_CLOEXEC | O_NOFOLLOW);
			if(fd < 0) {
				LOGE("open(%s) failed, err=%d", path, errno);
				return false;
			}

			size_t len = strlen(value);
			ssize_t written = write(fd, value, len);
			int savedErrno = errno;
			if(close(fd) < 0 && written == (ssize_t)len) {
				written = -1;
				savedErrno = errno;
			}
			if(written != (ssize_t)len) {
				LOGE("write(%s) failed, err=%d", path, savedErrno);
				return false;
			}
			return true;
		}

		void stop_audio_tone() {
			if(! mToneRunning.load(std::memory_order_acquire)) return;

			mToneRunning.store(false, std::memory_order_release);

			// Unblock a pending blocking snd_pcm_writei() in the tone thread.
			if(mAlsaPcm) {
				int err = snd_pcm_drop(mAlsaPcm);
				if(err < 0) {
					LOGW("snd_pcm_drop() failed while stopping the tone thread: %s", snd_strerror(err));
				}
			}

			if(mToneThread) {
				if(mToneThread->joinable()) mToneThread->join();
				delete mToneThread;
				mToneThread = NULL;
			}
		}

		void stop_alsa() {
			stop_audio_tone();

			if(! mAlsaPcm) return;

			int err = snd_pcm_drop(mAlsaPcm);
			if(err < 0) {
				LOGW("snd_pcm_drop() failed while stopping ALSA output: %s", snd_strerror(err));
			}
			err = snd_pcm_close(mAlsaPcm);
			if(err < 0) {
				LOGW("snd_pcm_close() failed while stopping ALSA output: %s", snd_strerror(err));
			}
			mAlsaPcm = NULL;
		}

		/*
		 * Fixed audio test pattern: 48 kHz / 16-bit / 2ch / 1 kHz sine tone,
		 * continuously written to the ALSA playback device.  The samples are
		 * staged in PS DDR memory and consumed by the xilinx audio formatter
		 * IP (selected with port_enable=0,0) which converts them to the AXI
		 * Stream audio format used by the HDMI TX.
		 */
		void audio_tone_thread_main() {
			static const double kPi = 3.14159265358979323846;
			const unsigned int sampleRate = 48000;
			const unsigned int channelCount = 2;
			const double toneFreq = 1000.0;
			const double amplitude = 16384.0; // -6 dBFS
			const snd_pcm_uframes_t periodFrames = 1024;

			const double twoPi = 2.0 * kPi;
			const double phaseStep = twoPi * toneFreq / sampleRate;

			std::vector<int16_t> frames((size_t)periodFrames * channelCount);

			double phase = 0.0;
			while(mToneRunning.load(std::memory_order_acquire)) {
				// Generate one period of the 1 kHz sine (both channels).
				for(snd_pcm_uframes_t i = 0; i < periodFrames; i++) {
					int16_t sample = (int16_t)std::llround(amplitude * std::sin(phase));
					phase += phaseStep;
					if(phase >= twoPi) phase -= twoPi;
					frames[(size_t)i * channelCount + 0] = sample;
					frames[(size_t)i * channelCount + 1] = sample;
				}

				snd_pcm_t* pcm = mAlsaPcm;
				if(! pcm) break;

				size_t remaining = (size_t)periodFrames;
				const int16_t* data = frames.data();
				while(remaining > 0 && mToneRunning.load(std::memory_order_acquire)) {
					snd_pcm_sframes_t written = snd_pcm_writei(pcm, data, remaining);
					if(written < 0) {
						int err = (int)written;
						if(err == -EPIPE) {
							// Underrun: rewind and keep feeding the tone.
							if(snd_pcm_prepare(pcm) < 0) break;
							continue;
						}
						if(err == -ESTRPIPE) {
							// Device suspended: wait for resume.
							while((err = snd_pcm_resume(pcm)) == -EAGAIN &&
								mToneRunning.load(std::memory_order_acquire)) {
								std::this_thread::sleep_for(std::chrono::milliseconds(10));
							}
							if(err < 0) snd_pcm_prepare(pcm);
							continue;
						}
						if(err == -EBADFD || err == -ENODEV) break;
						LOGW("ALSA tone: snd_pcm_writei() failed: %s", snd_strerror(err));
						std::this_thread::sleep_for(std::chrono::milliseconds(10));
						continue;
					}
					if(written == 0) {
						std::this_thread::sleep_for(std::chrono::milliseconds(1));
						continue;
					}
					data += written * channelCount;
					remaining -= (size_t)written;
				}
			}
		}

		bool start_test_pattern_playback() {
			static const char* alsaDevice = "default:CARD=xlnxhdmi1";

			stop_alsa();

			int err = snd_pcm_open(&mAlsaPcm, alsaDevice, SND_PCM_STREAM_PLAYBACK, 0);
			if(err < 0) {
				LOGE("snd_pcm_open(%s) failed: %s", alsaDevice, snd_strerror(err));
				mAlsaPcm = NULL;
				return false;
			}

			snd_pcm_hw_params_t* hwParams;
			snd_pcm_hw_params_alloca(&hwParams);
			if((err = snd_pcm_hw_params_any(mAlsaPcm, hwParams)) < 0 ||
				(err = snd_pcm_hw_params_set_access(mAlsaPcm, hwParams, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0 ||
				(err = snd_pcm_hw_params_set_format(mAlsaPcm, hwParams, SND_PCM_FORMAT_S16_LE)) < 0 ||
				(err = snd_pcm_hw_params_set_rate(mAlsaPcm, hwParams, 48000, 0)) < 0 ||
				(err = snd_pcm_hw_params_set_channels(mAlsaPcm, hwParams, 2)) < 0) {
				LOGE("Failed to configure ALSA test pattern HW params: %s", snd_strerror(err));
				stop_alsa();
				return false;
			}

			// Use a period/buffer size suited to the tone generator.
			snd_pcm_uframes_t period = 1024;
			snd_pcm_uframes_t buffer = 4096;
			err = snd_pcm_hw_params_set_period_size_near(mAlsaPcm, hwParams, &period, NULL);
			if(err < 0) LOGW("snd_pcm_hw_params_set_period_size_near() failed: %s", snd_strerror(err));
			err = snd_pcm_hw_params_set_buffer_size_near(mAlsaPcm, hwParams, &buffer);
			if(err < 0) LOGW("snd_pcm_hw_params_set_buffer_size_near() failed: %s", snd_strerror(err));

			if((err = snd_pcm_hw_params(mAlsaPcm, hwParams)) < 0) {
				LOGE("Failed to apply ALSA test pattern HW params: %s", snd_strerror(err));
				stop_alsa();
				return false;
			}

			/*
			 * Start playback automatically once one period has been written.
			 * Keep the default stop threshold so underruns are reported and
			 * recovered by the tone thread instead of being masked.
			 */
			snd_pcm_sw_params_t* swParams;
			snd_pcm_sw_params_alloca(&swParams);
			if((err = snd_pcm_sw_params_current(mAlsaPcm, swParams)) < 0 ||
				(err = snd_pcm_sw_params_set_start_threshold(mAlsaPcm, swParams, period)) < 0 ||
				(err = snd_pcm_sw_params(mAlsaPcm, swParams)) < 0) {
				LOGE("Failed to configure ALSA test pattern software parameters: %s", snd_strerror(err));
				stop_alsa();
				return false;
			}

			if((err = snd_pcm_prepare(mAlsaPcm)) < 0) {
				LOGE("Failed to prepare ALSA device %s: %s", alsaDevice, snd_strerror(err));
				stop_alsa();
				return false;
			}

			mToneRunning.store(true, std::memory_order_release);
			mToneThread = new std::thread(std::bind(&self_t::audio_tone_thread_main, this));

			LOGI("Started ALSA test pattern: 48 kHz / 16-bit / 2ch / 1 kHz tone on %s", alsaDevice);
			return true;
		}

		static bool configure_audio_switch(AudioMode mode) {
			static const char* switchRegUpdate =
				"/sys/devices/platform/amba_pl@0/b00a0000.axis_switch/reg_update_enable";
			static const char* switchPortEnable =
				"/sys/devices/platform/amba_pl@0/b00a0000.axis_switch/port_enable";

			const char* portEnable = NULL;
			const char* description = NULL;
			if(mode == AUDIO_MODE_TEST_PATTERN) {
				// xilinx audio formatter IP: reads PS DDR data written via ALSA.
				portEnable = "0,0";
				description = "xilinx audio formatter (PS DDR)";
			} else {
				// Fixed FPGA module that generates the audio data.
				portEnable = "0,1";
				description = "custom FPGA module";
			}

			if(! write_sysfs_value(switchRegUpdate, "0") ||
				! write_sysfs_value(switchPortEnable, portEnable) ||
				! write_sysfs_value(switchRegUpdate, "1")) {
				LOGE("Failed to configure audio stream switcher route (%s, port_enable=%s)",
					description, portEnable);
				return false;
			}

			LOGI("Audio stream switcher route: %s (port_enable=%s)", description, portEnable);
			return true;
		}

		static bool configure_dante_rx_to_aes3(int sample_width, int sample_rate) {
			static const char* switchApply =
				"/sys/devices/platform/amba_pl@0/b0040000.dante_rx_to_aes3/apply";
			static const char* switchSampleWidth =
				"/sys/devices/platform/amba_pl@0/b0040000.dante_rx_to_aes3/sample-width";
			static const char* switchSampleRate =
				"/sys/devices/platform/amba_pl@0/b0040000.dante_rx_to_aes3/sample-rate";

			char str_sample_width[16];
			char str_sample_rate[16];

			snprintf(str_sample_width, sizeof(str_sample_width), "%d", sample_width);
			snprintf(str_sample_rate, sizeof(str_sample_rate), "%d", sample_rate);

			if(! write_sysfs_value(switchSampleWidth, str_sample_width) ||
				! write_sysfs_value(switchSampleRate, str_sample_rate)) {
				LOGE("Failed to set dante_rx_to_aes3 sysfs properties (sample_width=%d, sample_rate=%d)",
					sample_width, sample_rate);
				return false;
			}

			const int max_retries = 3;
			bool apply_ok = false;

			for(int retry = 0; retry <= max_retries; ++retry) {
				if(retry > 0) {
					LOGW("Retrying write(%s) (attempt %d/%d)...", switchApply, retry, max_retries);
					usleep(500000);
				}
				if(write_sysfs_value(switchApply, "1")) {
					apply_ok = true;
					break;
				}
			}

			if(! apply_ok) {
				LOGE("Failed to configure dante_rx_to_aes3 (sample_width=%d, sample_rate=%d) after %d retries",
					sample_width, sample_rate, max_retries);
				return false;
			}

			LOGI("dante_rx_to_aes3: (sample_width=%d sample_rate=%d)", sample_width, sample_rate);
			return true;
		}

		bool set_audio_info(const struct audio_info* info) {
			static const char* alsaDevice = "default:CARD=xlnxhdmi1";

			if(! info) {
				LOGW("audio_info is NULL or has invalid audio properties");
				return false;
			}

			LOGD("audio_info: channel_count=%u, speaker_map=%d, level_shift_value=%u",
				info->channel_count, (int)info->speaker_map, info->level_shift_value);
			LOGD("audio_info: downmix_inhibit=%s, lfe_playback_level=%u, sample_rate=%u, sample_bits=%u",
				info->downmix_inhibit ? "true" : "false",
				info->lfe_playback_level, info->sample_rate, info->sample_bits);

			const char* encoding_str = "unknown";
			switch(info->encoding) {
			case AUDIO_EN_LPCM:      encoding_str = "LPCM"; break;
			case AUDIO_EN_AAC:       encoding_str = "AAC"; break;
			case AUDIO_EN_DTS:       encoding_str = "DTS"; break;
			case AUDIO_EN_DTS_HD:    encoding_str = "DTS_HD"; break;
			case AUDIO_EN_DD_AC3:    encoding_str = "DD_AC3"; break;
			case AUDIO_EN_DD_P_EAC3: encoding_str = "DD_P_EAC3"; break;
			case AUDIO_EN_D_THD_MAT: encoding_str = "D_THD_MAT"; break;
			case AUDIO_EN_MPEG:      encoding_str = "MPEG"; break;
			case AUDIO_EN_ATRAC:     encoding_str = "ATRAC"; break;
			case AUDIO_EN_WMA:       encoding_str = "WMA"; break;
			case AUDIO_EN_MPEG4:     encoding_str = "MPEG4"; break;
			}
			LOGD("audio_info: encoding=%d (%s)", (int)info->encoding, encoding_str);
			LOGI("audio output mode: %s",
				mAudioMode == AUDIO_MODE_TEST_PATTERN ? "test-pattern (fixed 48 kHz/16-bit/2ch 1 kHz tone)" : "stream (custom FPGA module)");

			// Route the audio stream to the source selected by the audio mode:
			//   stream       - custom FPGA module, port_enable = 0,1
			//   test-pattern - xilinx audio formatter (PS DDR via ALSA), port_enable = 0,0
			if(! configure_audio_switch(mAudioMode)) {
				return false;
			}

			// In test-pattern mode the output is fixed at 48 kHz / 16-bit / 2ch
			// 1 kHz tone regardless of the requested audio_info.
			if(mAudioMode == AUDIO_MODE_TEST_PATTERN) {
				if(! start_test_pattern_playback()) {
					LOGE("Failed to start audio test pattern playback");
					return false;
				}
				return true;
			}

			struct audio_info new_info = *info;

			if(new_info.channel_count == 0) {
				LOGW("invalid channel_count, use default value");
				new_info.channel_count = 8;
			}

			if(new_info.sample_rate == 0) {
				LOGW("invalid sample_rate, use default value");
				new_info.sample_rate = 48000;
			}

			if(new_info.sample_bits == 0) {
				LOGW("invalid sample_bits, use default value");
				new_info.sample_bits = 16;
			}

			snd_pcm_format_t format = SND_PCM_FORMAT_S16_LE;
			switch(new_info.sample_bits) {
			case 8:  format = SND_PCM_FORMAT_U8; break;
			case 16: format = SND_PCM_FORMAT_S16_LE; break;
			case 24: format = SND_PCM_FORMAT_S24_LE; break;
			case 32: format = SND_PCM_FORMAT_S32_LE; break;
			default:
				LOGE("Unsupported ALSA sample width: %u bits", new_info.sample_bits);
				return false;
			}

			stop_alsa();

			int err = snd_pcm_open(&mAlsaPcm, alsaDevice, SND_PCM_STREAM_PLAYBACK, 0);
			if(err < 0) {
				LOGE("snd_pcm_open(%s) failed: %s", alsaDevice, snd_strerror(err));
				mAlsaPcm = NULL;
				return false;
			}

			snd_pcm_hw_params_t* hwParams;
			snd_pcm_hw_params_alloca(&hwParams);
			if((err = snd_pcm_hw_params_any(mAlsaPcm, hwParams)) < 0 ||
				(err = snd_pcm_hw_params_set_access(mAlsaPcm, hwParams, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0 ||
				(err = snd_pcm_hw_params_set_format(mAlsaPcm, hwParams, format)) < 0) {
				LOGE("Failed to configure ALSA format/access: %s", snd_strerror(err));
				stop_alsa();
				return false;
			}

			unsigned int rate = new_info.sample_rate;
			if((err = snd_pcm_hw_params_set_rate(mAlsaPcm, hwParams, rate, 0)) < 0) {
				LOGE("Failed to configure ALSA properties (%u Hz): %s",
					new_info.sample_rate, snd_strerror(err));
				stop_alsa();
				return false;
			}

			if((err = snd_pcm_hw_params_set_channels(mAlsaPcm, hwParams, new_info.channel_count)) < 0) {
				LOGE("Failed to configure ALSA properties (%u channels): %s",
					new_info.channel_count, snd_strerror(err));
				stop_alsa();
				return false;
			}

			if((err = snd_pcm_hw_params(mAlsaPcm, hwParams)) < 0) {
				LOGE("Failed to configure ALSA properties: %s", snd_strerror(err));
				stop_alsa();
				return false;
			}

			/*
			 * The HDMI playback link is only used to configure the HDMI
			 * codec.  Its actual samples come from the custom AES3 IP stream
			 * selected above, so this ALSA playback buffer is intentionally
			 * empty.  ALSA normally rejects snd_pcm_start() with -EPIPE when
			 * the playback buffer has no application data.  Disable that
			 * empty-buffer start check for this externally-fed stream.
			 */
			snd_pcm_sw_params_t* swParams;
			snd_pcm_uframes_t boundary = 0;
			snd_pcm_uframes_t stopThreshold = 0;
			snd_pcm_sw_params_alloca(&swParams);
			if((err = snd_pcm_sw_params_current(mAlsaPcm, swParams)) < 0 ||
				(err = snd_pcm_sw_params_get_boundary(swParams, &boundary)) < 0 ||
				(err = snd_pcm_sw_params_get_stop_threshold(swParams, &stopThreshold)) < 0 ||
				(err = snd_pcm_sw_params_set_stop_threshold(mAlsaPcm, swParams, boundary)) < 0 ||
				(err = snd_pcm_sw_params(mAlsaPcm, swParams)) < 0) {
				LOGE("Failed to configure ALSA software parameters: %s", snd_strerror(err));
				stop_alsa();
				return false;
			}
			LOGD("ALSA playback software parameters: boundary=%lu stop_threshold=%lu -> %lu",
				(unsigned long)boundary, (unsigned long)stopThreshold,
				(unsigned long)boundary);

			if((err = snd_pcm_prepare(mAlsaPcm)) < 0) {
				LOGE("Failed to prepare ALSA device %s: %s", alsaDevice, snd_strerror(err));
				stop_alsa();
				return false;
			}

			LOGD("Starting ALSA device %s: state=%s avail=%ld",
				alsaDevice, snd_pcm_state_name(snd_pcm_state(mAlsaPcm)),
				(long)snd_pcm_avail_update(mAlsaPcm));
			if((err = snd_pcm_start(mAlsaPcm)) < 0) {
				LOGE("Failed to start ALSA device %s: %s (state=%s)", alsaDevice,
					snd_strerror(err), snd_pcm_state_name(snd_pcm_state(mAlsaPcm)));
				stop_alsa();
				return false;
			}

#if 1
			if(! configure_dante_rx_to_aes3(new_info.sample_bits, new_info.sample_rate)) {
				return false;
			}
#endif

			LOGI("Started ALSA device %s (%u channels, %u Hz, %u bits); audio data is supplied by the IP core",
				alsaDevice, new_info.channel_count, new_info.sample_rate, new_info.sample_bits);
			return true;
		}

		static void _set_hdr(struct dau_service *dau,
					struct DBusMessage *message,
					const struct hdr_info *hdr,
					void *ctxt)
		{
			LOGI("---tag---: %s", __FUNCTION__);

			if(hdr) {
				char hdrBytes[HDR_LEN * 3] = { 0 };
				char dvBytes[DV_LEN * 3] = { 0 };

				format_bytes(hdrBytes, sizeof(hdrBytes), hdr->hdr, HDR_LEN);
				format_bytes(dvBytes, sizeof(dvBytes), hdr->dv, DV_LEN);

				LOGI("hdr_info: avi_c=%u, avi_ec=%u, avi_q=%u, dv_tunneling=%s",
					(unsigned int)hdr->avi_c, (unsigned int)hdr->avi_ec, (unsigned int)hdr->avi_q,
					hdr->dv_tunneling ? "true" : "false");
				LOGD("hdr_info: hdr[%u]=%s", (unsigned int)HDR_LEN, hdrBytes);
				LOGD("hdr_info: dv[%u]=%s", (unsigned int)DV_LEN, dvBytes);
			} else {
				LOGW("hdr_info is NULL");
			}

			dau_reply_success(dau, message);
		}

		static void _get_edid(struct dau_service *dau,
					 struct DBusMessage *message,
					 void *ctxt)
		{
			self_t* pThis = (self_t*)ctxt;

			pThis->get_edid(dau, message);
		}

		void get_edid(struct dau_service *dau, struct DBusMessage *message) {
			LOGI("---tag---: %s", __FUNCTION__);

			unsigned char edid[EDID_LEN] = { 0 };
			bool edidValid = false;
			size_t edidLen = 0;

			if(mDrmFd >= 0 && mConnectorId > 0) {
				LOGD("drm_fd=%d, connector_id=%u", mDrmFd, mConnectorId);

				std::shared_ptr<drmModeObjectProperties> props(
					drmModeObjectGetProperties(mDrmFd, mConnectorId, DRM_MODE_OBJECT_CONNECTOR),
					drmModeFreeObjectProperties);
				if(! props) {
					LOGW("drmModeObjectGetProperties() returned NULL");
				} else {
					LOGD("connector has %u properties", props->count_props);
					uint32_t propId = get_prop_id(mDrmFd, props.get(), NULL, "EDID");
					uint64_t blobId = 0;

					if(propId == 0) {
						LOGW("EDID property not found on connector");
					} else if(! get_prop_value(props.get(), propId, &blobId)) {
						LOGW("EDID property value is unavailable");
					} else {
						LOGD("EDID property found, id=%u, blob id=%llu",
							propId, (unsigned long long)blobId);
						if(blobId == 0) {
							LOGW("EDID blob id is zero");
						} else {
							std::shared_ptr<drmModePropertyBlobRes> blob(
								drmModeGetPropertyBlob(mDrmFd, blobId),
								drmModeFreePropertyBlob);
							if(! blob || ! blob->data || blob->length == 0) {
								LOGW("EDID blob is null or empty");
							} else {
								const unsigned char* blobData = (const unsigned char*)blob->data;
								size_t blobLen = (size_t)blob->length;
								LOGD("EDID blob length=%zu", blobLen);

								if(blobLen >= 8) {
									LOGD("EDID header: %02x %02x %02x %02x %02x %02x %02x %02x",
										blobData[0], blobData[1], blobData[2], blobData[3],
										blobData[4], blobData[5], blobData[6], blobData[7]);
								}

								if(! write_edid_blob(blobData, blobLen)) {
									LOGW("Failed to save EDID blob");
								}

								size_t copyLen = blobLen < sizeof(edid) ? blobLen : sizeof(edid);
								memcpy(edid, blobData, copyLen);
								edidLen = copyLen;
								edidValid = is_valid_edid(edid, edidLen);
								if(! edidValid) {
									LOGW("EDID blob is present but invalid");
								}
							}
						}
					}
				}
			} else {
				LOGW("DRM not available (fd=%d, connector=%u)", mDrmFd, mConnectorId);
			}

			if(! edidValid) {
				LOGW("No valid EDID is available");
				dau_reply_error(dau, message, "No valid EDID is available");
				return;
			}

			LOGI("Returning valid EDID (%zu bytes copied, %u bytes returned)",
				edidLen, (unsigned int)EDID_LEN);
			dau_reply_get_edid(dau, message, edid);
		}

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

					switch(ch) {
						break;
					}

					return true;
				}, 1000000LL, 10LL);
			}

			_FreeStack_main_.flush();
		}

		QRETURN OnStart(free_stack_t& _FreeStack_, QRESULT& qres) {
			int err;

			qres = QCAP_RS_SUCCESSFUL;

			switch(1) { case 1:
				mDrmFd = -1;
				mConnectorId = 0;
				mCrtcId = 0;
				mPrimaryPlaneId = 0;
				mPrimaryFbId = 0;
				mPrimaryBoHandle = 0;
				mOverlayPlaneId = 34;
				mOverlayFbId = 0;
				mOverlayBoHandle = 0;
				mLastConnectorConnected = false;
				mHaveLastConnectorState = false;
				mAlsaPcm = NULL;
				mToneThread = NULL;
				mToneRunning.store(false);
				_FreeStack_ += [this]() {
					stop_alsa();
				};
				init_default_video_format(&mCurrentVideoFormat);
				mAudioMode = audio_mode_from_env();
				LOGI("audio output mode selected by env: %s",
					mAudioMode == AUDIO_MODE_TEST_PATTERN ? "test-pattern" : "stream");

				{
					int drm_fd = drmOpen("xlnx", NULL);
					if(drm_fd < 0) {
						err = errno;
						qres = QCAP_RS_ERROR_GENERAL;
						LOGE("%s(%d): drmOpen() failed, err=%d", __FUNCTION__, __LINE__, err);
						break;
					}
					_FreeStack_ += [drm_fd]() {
						drmClose(drm_fd);
					};
					mDrmFd = drm_fd;
					_FreeStack_ += [this]() {
						cleanup_drm_fbs();
					};

					err = drmSetClientCap(drm_fd, DRM_CLIENT_CAP_ATOMIC, 1);
					if(err) {
						err = errno;
						qres = QCAP_RS_ERROR_GENERAL;
						LOGE("%s(%d): drmSetClientCap(DRM_CLIENT_CAP_ATOMIC) failed, err=%d", __FUNCTION__, __LINE__, err);
						break;
					}

					err = drmSetClientCap(drm_fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
					if(err) {
						err = errno;
						qres = QCAP_RS_ERROR_GENERAL;
						LOGE("%s(%d): drmSetClientCap(DRM_CLIENT_CAP_UNIVERSAL_PLANES) failed, err=%d", __FUNCTION__, __LINE__, err);
						break;
					}

					std::shared_ptr<drmModeRes> pResources(drmModeGetResources(drm_fd), drmModeFreeResources);
					if(! pResources) {
						qres = QCAP_RS_ERROR_GENERAL;
						LOGE("%s(%d): drmModeGetResources() failed", __FUNCTION__, __LINE__);
						break;
					}

					for(int i = 0; i < pResources->count_connectors; i++) {
						std::shared_ptr<drmModeConnector> pConnector(
							drmModeGetConnector(drm_fd, pResources->connectors[i]),
							drmModeFreeConnector);
						if(! pConnector) continue;

						LOGD("connector: id=%d type=%d type_id=%d",
							pConnector->connector_id, pConnector->connector_type,
							pConnector->connector_type_id);

						if(pConnector->connector_type == DRM_MODE_CONNECTOR_HDMIA &&
							pConnector->connector_type_id == 1) {
							mConnectorId = pConnector->connector_id;
							break;
						}
					}

					if(mConnectorId == 0) {
						qres = QCAP_RS_ERROR_GENERAL;
						LOGE("%s(%d): HDMI-A-1 connector not found", __FUNCTION__, __LINE__);
						break;
					}

					LOGI("HDMI-A-1 connector found, id=%u", mConnectorId);
				}

				dau_service* pHdmiTxDauServ;
				qres = StartDauServ(_FreeStack_, &pHdmiTxDauServ);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): StartDauServ() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				this->pHdmiTxDauServ = pHdmiTxDauServ;

				dau_signal_cable_change(pHdmiTxDauServ);

				qres = StartConnectorMonitor(_FreeStack_, pHdmiTxDauServ);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): StartConnectorMonitor() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				if(mAudioMode == AUDIO_MODE_TEST_PATTERN) {
					// Best-effort: start the fixed test tone immediately so the
					// audio path can be verified without host-driven
					// set_audio_info().  If it fails (e.g. HDMI link not up yet),
					// set_audio_info() will retry later.
					if(configure_audio_switch(mAudioMode) && start_test_pattern_playback()) {
						LOGI("Audio test pattern playback started at startup");
					} else {
						LOGW("Audio test pattern playback failed at startup; will retry on set_audio_info()");
					}
				}
			}
			return QCAP_RT_OK;
		}

		static int open_drm_uevent_socket() {
			int fd = socket(AF_NETLINK, SOCK_DGRAM, NETLINK_KOBJECT_UEVENT);
			if(fd < 0) return -1;

			int flags = fcntl(fd, F_GETFL, 0);
			if(flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
			flags = fcntl(fd, F_GETFD, 0);
			if(flags >= 0) fcntl(fd, F_SETFD, flags | FD_CLOEXEC);

			struct sockaddr_nl addr;
			memset(&addr, 0x00, sizeof(addr));
			addr.nl_family = AF_NETLINK;
			addr.nl_pid = (uint32_t)getpid();
			addr.nl_groups = 1;

			if(bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
				int err = errno;
				close(fd);
				errno = err;
				return -1;
			}

			return fd;
		}

		static bool is_target_connector_uevent(const char* buf, ssize_t len) {
			bool isDrm = false;
			bool isHotplug = false;
			bool hasConnectorName = false;
			bool connectorMatched = false;

			for(ssize_t off = 0; off < len; ) {
				const char* s = buf + off;
				size_t left = (size_t)(len - off);
				size_t n = strnlen(s, left);
				if(n == 0) {
					off++;
					continue;
				}

				if(strcmp(s, "SUBSYSTEM=drm") == 0) {
					isDrm = true;
				} else if(strcmp(s, "HOTPLUG=1") == 0) {
					isHotplug = true;
				} else if(strncmp(s, "CONNECTOR=", 10) == 0) {
					hasConnectorName = true;
					connectorMatched = (strcmp(s + 10, "HDMI-A-1") == 0);
				} else if(strncmp(s, "DEVPATH=", 8) == 0 || strncmp(s, "change@", 7) == 0) {
					if(strstr(s, "HDMI-A-1") != NULL) {
						connectorMatched = true;
					}
				}

				off += (ssize_t)n + 1;
			}

			return isDrm && isHotplug && (! hasConnectorName || connectorMatched);
		}

		void update_connector_state(dau_service* dauserv, const char* reason) {
			bool connected = false;
			if(! query_connector_connected(connected)) {
				LOGW("failed to query DRM connector state (%s)", reason ? reason : "event");
				return;
			}

			if(! mHaveLastConnectorState || connected != mLastConnectorConnected) {
				LOGI("DRM connector state changed: %s -> %s (%s)",
					mHaveLastConnectorState ? (mLastConnectorConnected ? "connected" : "disconnected") : "unknown",
					connected ? "connected" : "disconnected",
					reason ? reason : "event");

				mLastConnectorConnected = connected;
				mHaveLastConnectorState = true;

				if(dauserv) {
					dau_signal_cable_change(dauserv);
				}
			} else {
				LOGD("DRM connector state unchanged after %s: %s",
					reason ? reason : "event", connected ? "connected" : "disconnected");
			}
		}

		QRESULT StartConnectorMonitor(free_stack_t& _FreeStack_, dau_service* dauserv) {
			QRESULT qres = QCAP_RS_SUCCESSFUL;

			switch(1) { case 1:
				bool connected = false;
				if(query_connector_connected(connected)) {
					mLastConnectorConnected = connected;
					mHaveLastConnectorState = true;
					LOGI("Initial DRM connector state: %s", connected ? "connected" : "disconnected");
				} else {
					LOGW("Unable to read initial DRM connector state");
				}

				int fdUevent = open_drm_uevent_socket();
				if(fdUevent < 0) {
					int err = errno;
					qres = QCAP_RS_ERROR_GENERAL;
					LOGE("%s(%d): open_drm_uevent_socket() failed, err=%d", __FUNCTION__, __LINE__, err);
					break;
				}
				_FreeStack_ += [fdUevent]() {
					close(fdUevent);
				};

				qres = AddEventHandler(_FreeStack_, fdUevent,
					std::bind(&self_t::OnConnectorMonitor, this, fdUevent, dauserv));
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): AddEventHandler() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				LOGI("DRM connector monitor started in event-driven mode (uevent fd=%d)", fdUevent);
			}

			return qres;
		}

		QRETURN OnConnectorMonitor(int fdUevent, dau_service* dauserv) {
			switch(1) { case 1:
				bool haveConnectorEvent = false;

				for(;;) {
					char buf[4096];
					ssize_t n = recv(fdUevent, buf, sizeof(buf), 0);
					if(n < 0) {
						int err = errno;
						if(err == EAGAIN || err == EWOULDBLOCK) break;
						LOGE("%s(%d): recv(uevent) failed, err=%d", __FUNCTION__, __LINE__, err);
						break;
					}
					if(n == 0) break;

					if(is_target_connector_uevent(buf, n)) {
						haveConnectorEvent = true;
					}
				}

				if(haveConnectorEvent) {
					update_connector_state(dauserv, "DRM hotplug uevent");
				}
			}

			return QCAP_RT_OK;
		}

		QRESULT StartDauServ(free_stack_t& _FreeStack_, dau_service** ppHdmiTxDauServ) {
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

				struct dau_service* hdmitx = dau_service_register(DAU_SERVICE_SINK, 0, &methods, this);
				if (! hdmitx) {
					qres = QCAP_RS_ERROR_GENERAL;
					LOGE("%s(%d): dau_service_register() failed", __FUNCTION__, __LINE__);
					break;
				}
				_FreeStack_ += [hdmitx]() {
					dau_service_unregister(hdmitx);
				};

				int fd_hdmitx = dau_service_get_fd(hdmitx);
				LOGD("fd_hdmitx=%d", fd_hdmitx);

				qres = AddEventHandler(_FreeStack_, fd_hdmitx, std::bind(&self_t::OnHdmiTxEvent, this, hdmitx));
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): AddEventHandler() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				*ppHdmiTxDauServ = hdmitx;
			}

			return qres;
		}

		QRETURN OnHdmiTxEvent(dau_service* dauserv) {
			// LOGI("---tag---: %s +dau_service_dispatch", __FUNCTION__);
			dau_service_dispatch(dauserv, 0);
			// LOGI("---tag---: %s -dau_service_dispatch", __FUNCTION__);

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
