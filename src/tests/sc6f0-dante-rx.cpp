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

	// Audio routing is exposed by the axis_switch ASoC component as an ALSA
	// mixer enum.  The Dante RX to AES3 converter is configured separately by
	// the ASoC DAI hw_params callback when this application applies PCM params.
	enum AudioMode {
		AUDIO_MODE_STREAM = 0,
		AUDIO_MODE_TEST_PATTERN = 1,
	};

	struct AudioSwitch {
		static bool select_source(AudioMode mode) {
			static const char* alsaCard = "hw:CARD=xlnxhdmi1";
			static const char* controlName = "Audio Input Route Switch";
			const unsigned int route = mode == AUDIO_MODE_STREAM ? 1 : 0;
			const char* description = route == 1
				? "Custom FPGA (Dante RX)"
				: "PS DDR (Audio Formatter)";

			snd_ctl_t* controlHandle = NULL;
			int err = snd_ctl_open(&controlHandle, alsaCard, 0);
			if(err < 0) {
				LOGE("snd_ctl_open(%s) failed: %s", alsaCard, snd_strerror(err));
				return false;
			}

			snd_ctl_elem_id_t* id;
			snd_ctl_elem_value_t* value;
			snd_ctl_elem_id_alloca(&id);
			snd_ctl_elem_value_alloca(&value);
			snd_ctl_elem_id_set_interface(id, SND_CTL_ELEM_IFACE_MIXER);
			snd_ctl_elem_id_set_name(id, controlName);
			snd_ctl_elem_value_set_id(value, id);
			snd_ctl_elem_value_set_enumerated(value, 0, route);

			err = snd_ctl_elem_write(controlHandle, value);
			if(err < 0) {
				LOGE("Failed to set ALSA mixer control '%s' on %s: %s",
					controlName, alsaCard, snd_strerror(err));
				snd_ctl_close(controlHandle);
				return false;
			}

			err = snd_ctl_close(controlHandle);
			if(err < 0) {
				LOGW("snd_ctl_close(%s) failed: %s", alsaCard, snd_strerror(err));
			}

			LOGI("Audio stream switcher route: %s (enum item=%u)", description, route);
			return true;
		}
	};
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

	static int test_case_from_args() {
		int testCase = 1;

		for(int i = 1; i < g_argc; i++) {
			const char* arg = g_argv[i];
			if(! arg) continue;

			const char* value = NULL;
			if(strcmp(arg, "--test") == 0) {
				if(i + 1 >= g_argc) {
					LOGE("--test requires a test case number");
					return -1;
				}
				value = g_argv[++i];
			} else if(strncmp(arg, "--test=", 7) == 0) {
				value = arg + 7;
			} else {
				LOGW("Ignoring unknown argument: %s", arg);
				continue;
			}

			char* end = NULL;
			long parsed = strtol(value, &end, 10);
			if(value[0] == '\0' || ! end || *end != '\0' || parsed < 1 || parsed > 2) {
				LOGE("Invalid test case '%s' (expected 1 or 2)", value);
				return -1;
			}
			testCase = (int)parsed;
		}

		return testCase;
	}

	int Run() {
		const int testCase = test_case_from_args();
		if(testCase < 0) {
			LOGE("Usage: sc6f0-dante-rx [--test N]");
			return 2;
		}

		return DoCase(testCase);
	}

	int DoCase(int testCase) {
		LOGI("Running test case %d", testCase);
		switch(testCase) {
		case 1:
			mTestCase1.DoWork();
			return 0;
		case 2:
			mTestCase2.DoWork();
			return 0;
		default:
			LOGE("Unsupported test case %d", testCase);
			return 2;
		}
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
			 * SC6F0_DANTE_RX_AUDIO_MODE selects the ASoC mixer route:
			 *   stream      (default) - item 1, Custom FPGA (Dante RX)
			 *   testpattern           - item 0, PS DDR (Audio Formatter)
			 */
			const char* strVal = getenv("SC6F0_DANTE_RX_AUDIO_MODE");
			if(strVal) {
				if(strcmp(strVal, "stream") == 0) return AUDIO_MODE_STREAM;
				if(strcmp(strVal, "testpattern") == 0) return AUDIO_MODE_TEST_PATTERN;
				LOGW("SC6F0_DANTE_RX_AUDIO_MODE=%s is unknown; using default (stream)", strVal);
			}
			return AUDIO_MODE_STREAM;
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
		 * IP (selected with mixer enum item 0) which converts them to the AXI
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

			// Route through the axis_switch ASoC mixer control:
			//   stream       - item 1, Custom FPGA (Dante RX)
			//   test-pattern - item 0, PS DDR (Audio Formatter)
			if(! AudioSwitch::select_source(mAudioMode)) {
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
			case 16: format = SND_PCM_FORMAT_S16_LE; break;
			case 20: format = SND_PCM_FORMAT_S20_LE; break;
			case 24: format = SND_PCM_FORMAT_S24_LE; break;
			default:
				LOGE("Unsupported Dante RX AES3 sample width: %u bits (expected 16, 20, or 24)",
					new_info.sample_bits);
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

			// Applying hw_params invokes the dante_rx_aes3 ASoC DAI callback,
			// which programs sample rate/width and performs the hardware retries.
			if((err = snd_pcm_hw_params(mAlsaPcm, hwParams)) < 0) {
				LOGE("Failed to apply ALSA HW params (%u channels, %u Hz, %u bits): %s",
					new_info.channel_count, new_info.sample_rate, new_info.sample_bits,
					snd_strerror(err));
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
					if(AudioSwitch::select_source(mAudioMode) && start_test_pattern_playback()) {
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

	/*
	 * Test case 2 is intentionally independent from the DAU service above.  It
	 * drives the HDMI TX directly: a DRM NV12 framebuffer containing SMPTE
	 * colour bars is assigned to plane 36, while ALSA supplies the fixed
	 * 48 kHz/16-bit/2-channel test audio.
	 */
	struct TestCase2 : public TestCase {
		typedef TestCase2 self_t;
		typedef TestCase super_t;

		int mDrmFd;
		uint32_t mConnectorId;
		uint32_t mCrtcId;
		uint32_t mPlaneId;
		uint32_t mPrimaryPlaneId;
		uint32_t mFbId;
		uint32_t mBoHandle;
		uint32_t mPrimaryFbId;
		uint32_t mPrimaryBoHandle;
		uint32_t mModeBlobId;
		uint32_t mPitch;
		uint32_t mPrimaryPitch;
		uint32_t mModePropId;
		uint32_t mActivePropId;
		uint32_t mConnectorCrtcPropId;
		uint32_t mPlaneFbPropId;
		uint32_t mPlaneCrtcPropId;
		uint32_t mPlaneCrtcWPropId;
		uint32_t mPlaneCrtcHPropId;
		uint32_t mPlaneSrcWPropId;
		uint32_t mPlaneSrcHPropId;
		uint32_t mPrimaryFbPropId;
		uint32_t mPrimaryCrtcPropId;
		uint32_t mPrimaryCrtcWPropId;
		uint32_t mPrimaryCrtcHPropId;
		uint32_t mPrimarySrcWPropId;
		uint32_t mPrimarySrcHPropId;
		size_t mDumbSize;
		size_t mPrimaryDumbSize;
		void* mDumbMap;
		void* mPrimaryDumbMap;
		qcap2_audio_source_t* mAudioSource;
		qcap2_audio_sink_t* mAudioSink;

		static uint32_t prop_id(int fd, drmModeObjectProperties* props, const char* name) {
			return __sc6f0_dante_rx__::get_prop_id(fd, props, NULL, name);
		}

		static bool add_prop(drmModeAtomicReq* req, uint32_t object, uint32_t prop,
			const char* name, uint64_t value) {
			if(! prop || drmModeAtomicAddProperty(req, object, prop, value) < 0) {
				LOGE("TestCase2: DRM property %s failed on object %u: %s",
					name, object, strerror(errno));
				return false;
			}
			return true;
		}

		void init_state() {
			mDrmFd = -1; mConnectorId = 0; mCrtcId = 0; mPlaneId = 36;
			mPrimaryPlaneId = 0; mFbId = 0; mBoHandle = 0;
			mPrimaryFbId = 0; mPrimaryBoHandle = 0;
			mModeBlobId = 0; mPitch = 0; mPrimaryPitch = 0;
			mModePropId = mActivePropId = mConnectorCrtcPropId = 0;
			mPlaneFbPropId = mPlaneCrtcPropId = 0;
			mPlaneCrtcWPropId = mPlaneCrtcHPropId = 0;
			mPlaneSrcWPropId = mPlaneSrcHPropId = 0;
			mPrimaryFbPropId = mPrimaryCrtcPropId = 0;
			mPrimaryCrtcWPropId = mPrimaryCrtcHPropId = 0;
			mPrimarySrcWPropId = mPrimarySrcHPropId = 0;
			mDumbSize = mPrimaryDumbSize = 0;
			mDumbMap = MAP_FAILED; mPrimaryDumbMap = MAP_FAILED;
			mAudioSource = NULL; mAudioSink = NULL;
		}

		static void fill_smpte_nv12(void* ptr, uint32_t pitch, uint32_t width,
			uint32_t height) {
			static const uint8_t top[3][7] = {
				{180,162,131,112,84,65,35}, {128,44,156,72,184,100,212},
				{128,142,44,58,198,212,114}
			};
			static const uint8_t middle[3][7] = {
				{35,16,84,16,131,16,180}, {212,128,184,128,156,128,128},
				{114,128,198,128,44,128,128}
			};
			static const uint8_t bottom[3][7] = {
				{35,235,35,16,7,16,25}, {164,128,151,128,128,128,128},
				{116,128,165,128,128,128,128}
			};
			uint8_t* base = (uint8_t*)ptr;
			for(uint32_t y = 0; y < height; y++) {
				const uint8_t (*b)[7] = y < height * 2 / 3 ? top :
					(y < height * 3 / 4 ? middle : bottom);
				uint8_t* row = base + (size_t)y * pitch;
				for(uint32_t x = 0; x < width; x++) row[x] = b[0][(uint64_t)x * 7 / width];
			}
			uint8_t* uv = base + (size_t)pitch * height;
			for(uint32_t y = 0; y < height / 2; y++) {
				const uint8_t (*b)[7] = y * 2 < height * 2 / 3 ? top :
					(y * 2 < height * 3 / 4 ? middle : bottom);
				uint8_t* row = uv + (size_t)y * pitch;
				for(uint32_t x = 0; x < width; x += 2) {
					uint32_t n = (uint64_t)x * 7 / width;
					row[x] = b[1][n]; row[x + 1] = b[2][n];
				}
			}
		}

		static bool is_primary_plane(int fd, drmModePlane* plane, int crtcIndex) {
			if(! plane || !(plane->possible_crtcs & (1 << crtcIndex))) return false;
			std::shared_ptr<drmModeObjectProperties> props(
				drmModeObjectGetProperties(fd, plane->plane_id, DRM_MODE_OBJECT_PLANE),
				drmModeFreeObjectProperties);
			std::shared_ptr<drmModePropertyRes> typeProp;
			uint32_t typeId = prop_id(fd, props.get(), "type");
			if(props && typeId) typeProp.reset(drmModeGetProperty(fd, typeId), drmModeFreeProperty);
			if(! props || ! typeProp) return false;
			for(uint32_t i = 0; i < props->count_props; i++) {
				if(props->props[i] != typeId) continue;
				uint64_t value = props->prop_values[i];
				for(int j = 0; j < typeProp->count_enums; j++) {
					if(typeProp->enums[j].value == value &&
						strcmp(typeProp->enums[j].name, "Primary") == 0) return true;
				}
			}
			return false;
		}

		bool setup_drm() {
			const uint32_t width = 3840, height = 2160;
			mDrmFd = drmOpen("xlnx", NULL);
			if(mDrmFd < 0) { LOGE("TestCase2: drmOpen failed: %s", strerror(errno)); return false; }
			if(drmSetClientCap(mDrmFd, DRM_CLIENT_CAP_ATOMIC, 1) ||
				drmSetClientCap(mDrmFd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1)) {
				LOGE("TestCase2: DRM client caps failed: %s", strerror(errno)); cleanup_drm(); return false;
			}
			std::shared_ptr<drmModeRes> res(drmModeGetResources(mDrmFd), drmModeFreeResources);
			if(! res) { cleanup_drm(); return false; }
			std::shared_ptr<drmModeConnector> conn;
			for(int i = 0; i < res->count_connectors && !conn; i++) {
				std::shared_ptr<drmModeConnector> c(drmModeGetConnector(mDrmFd, res->connectors[i]), drmModeFreeConnector);
				if(c && c->connection == DRM_MODE_CONNECTED && c->count_modes) conn = c;
			}
			if(! conn) { LOGE("TestCase2: no connected DRM connector"); cleanup_drm(); return false; }
			mConnectorId = conn->connector_id;

			drmModeModeInfo mode = {};
			bool found = false;
			for(int i = 0; i < conn->count_modes; i++) {
				drmModeModeInfo* candidate = &conn->modes[i];
				if(candidate->hdisplay == (int)width && candidate->vdisplay == (int)height &&
					fabs(mode_vrefresh(candidate) - 60.0) < 0.5) {
					mode = *candidate; found = true;
					if(candidate->type & DRM_MODE_TYPE_PREFERRED) break;
				}
			}
			if(! found) { LOGE("TestCase2: no 3840x2160@60 mode"); cleanup_drm(); return false; }

			int crtcIndex = -1;
			for(int i = 0; i < conn->count_encoders && crtcIndex < 0; i++) {
				std::shared_ptr<drmModeEncoder> enc(drmModeGetEncoder(mDrmFd, conn->encoders[i]), drmModeFreeEncoder);
				if(! enc) continue;
				for(int j = 0; j < res->count_crtcs; j++) if(enc->possible_crtcs & (1 << j)) {
					crtcIndex = j; mCrtcId = res->crtcs[j]; break;
				}
			}
			if(crtcIndex < 0) { LOGE("TestCase2: no CRTC"); cleanup_drm(); return false; }
			std::shared_ptr<drmModePlane> plane(drmModeGetPlane(mDrmFd, mPlaneId), drmModeFreePlane);
			if(! plane || !(plane->possible_crtcs & (1 << crtcIndex))) {
				LOGE("TestCase2: plane %u cannot use CRTC %u", mPlaneId, mCrtcId); cleanup_drm(); return false;
			}
			bool supportsNv12 = false;
			for(uint32_t i = 0; i < plane->count_formats; i++) supportsNv12 |= plane->formats[i] == DRM_FORMAT_NV12;
			if(! supportsNv12) { LOGE("TestCase2: plane %u has no NV12 format", mPlaneId); cleanup_drm(); return false; }

			std::shared_ptr<drmModePlane> primary;
			std::shared_ptr<drmModePlaneRes> planeRes(
				drmModeGetPlaneResources(mDrmFd), drmModeFreePlaneResources);
			if(planeRes) {
				for(uint32_t i = 0; i < planeRes->count_planes && !primary; i++) {
					std::shared_ptr<drmModePlane> candidate(
						drmModeGetPlane(mDrmFd, planeRes->planes[i]), drmModeFreePlane);
					if(is_primary_plane(mDrmFd, candidate.get(), crtcIndex)) primary = candidate;
				}
			}
			if(! primary) {
				LOGE("TestCase2: no primary plane for CRTC %u", mCrtcId);
				cleanup_drm();
				return false;
			}
			mPrimaryPlaneId = primary->plane_id;
			bool supportsBgr888 = false;
			for(uint32_t i = 0; i < primary->count_formats; i++)
				supportsBgr888 |= primary->formats[i] == DRM_FORMAT_BGR888;
			if(! supportsBgr888) {
				LOGE("TestCase2: primary plane %u has no BGR888 format", mPrimaryPlaneId);
				cleanup_drm();
				return false;
			}

			struct drm_mode_create_dumb primaryCreate = {};
			primaryCreate.width = width; primaryCreate.height = height; primaryCreate.bpp = 24;
			if(drmIoctl(mDrmFd, DRM_IOCTL_MODE_CREATE_DUMB, &primaryCreate) < 0) {
				LOGE("TestCase2: primary CREATE_DUMB failed: %s", strerror(errno));
				cleanup_drm();
				return false;
			}
			mPrimaryBoHandle = primaryCreate.handle;
			mPrimaryPitch = primaryCreate.pitch;
			mPrimaryDumbSize = primaryCreate.size;
			struct drm_mode_map_dumb primaryMap = {};
			primaryMap.handle = mPrimaryBoHandle;
			if(drmIoctl(mDrmFd, DRM_IOCTL_MODE_MAP_DUMB, &primaryMap) < 0) {
				LOGE("TestCase2: primary MAP_DUMB failed: %s", strerror(errno));
				cleanup_drm();
				return false;
			}
			mPrimaryDumbMap = mmap(NULL, mPrimaryDumbSize, PROT_READ | PROT_WRITE,
				MAP_SHARED, mDrmFd, primaryMap.offset);
			if(mPrimaryDumbMap == MAP_FAILED) {
				LOGE("TestCase2: primary framebuffer mmap failed");
				cleanup_drm();
				return false;
			}
			memset(mPrimaryDumbMap, 0, mPrimaryDumbSize);
			uint32_t primaryHandles[4] = {mPrimaryBoHandle, 0, 0, 0};
			uint32_t primaryPitches[4] = {mPrimaryPitch, 0, 0, 0};
			uint32_t primaryOffsets[4] = {0, 0, 0, 0};
			if(drmModeAddFB2(mDrmFd, width, height, DRM_FORMAT_BGR888,
				primaryHandles, primaryPitches, primaryOffsets, &mPrimaryFbId, 0) < 0) {
				LOGE("TestCase2: primary AddFB2(BGR888) failed: %s", strerror(errno));
				cleanup_drm();
				return false;
			}

			struct drm_mode_create_dumb create = {};
			create.width = width; create.height = height * 3 / 2; create.bpp = 8;
			if(drmIoctl(mDrmFd, DRM_IOCTL_MODE_CREATE_DUMB, &create) < 0) {
				LOGE("TestCase2: CREATE_DUMB failed: %s", strerror(errno)); cleanup_drm(); return false;
			}
			mBoHandle = create.handle; mPitch = create.pitch; mDumbSize = create.size;
			struct drm_mode_map_dumb map = {}; map.handle = mBoHandle;
			if(drmIoctl(mDrmFd, DRM_IOCTL_MODE_MAP_DUMB, &map) < 0) {
				LOGE("TestCase2: MAP_DUMB failed: %s", strerror(errno)); cleanup_drm(); return false;
			}
			mDumbMap = mmap(NULL, mDumbSize, PROT_READ | PROT_WRITE, MAP_SHARED, mDrmFd, map.offset);
			if(mDumbMap == MAP_FAILED || mDumbSize < (size_t)mPitch * height * 3 / 2) {
				LOGE("TestCase2: NV12 dumb buffer mmap/size failed"); cleanup_drm(); return false;
			}
			fill_smpte_nv12(mDumbMap, mPitch, width, height);

			uint32_t handles[4] = {mBoHandle, mBoHandle, 0, 0};
			uint32_t pitches[4] = {mPitch, mPitch, 0, 0};
			uint32_t offsets[4] = {0, mPitch * height, 0, 0};
			if(drmModeAddFB2(mDrmFd, width, height, DRM_FORMAT_NV12, handles, pitches, offsets, &mFbId, 0) < 0) {
				LOGE("TestCase2: AddFB2 NV12 failed: %s", strerror(errno)); cleanup_drm(); return false;
			}
			std::shared_ptr<drmModeObjectProperties> cp(drmModeObjectGetProperties(mDrmFd, mCrtcId, DRM_MODE_OBJECT_CRTC), drmModeFreeObjectProperties);
			std::shared_ptr<drmModeObjectProperties> kp(drmModeObjectGetProperties(mDrmFd, mConnectorId, DRM_MODE_OBJECT_CONNECTOR), drmModeFreeObjectProperties);
			std::shared_ptr<drmModeObjectProperties> pp(drmModeObjectGetProperties(mDrmFd, mPlaneId, DRM_MODE_OBJECT_PLANE), drmModeFreeObjectProperties);
			std::shared_ptr<drmModeObjectProperties> primaryProps(drmModeObjectGetProperties(mDrmFd, mPrimaryPlaneId, DRM_MODE_OBJECT_PLANE), drmModeFreeObjectProperties);
			if(!cp || !kp || !pp || !primaryProps) { LOGE("TestCase2: DRM properties unavailable"); cleanup_drm(); return false; }
			mModePropId = prop_id(mDrmFd, cp.get(), "MODE_ID"); mActivePropId = prop_id(mDrmFd, cp.get(), "ACTIVE");
			mConnectorCrtcPropId = prop_id(mDrmFd, kp.get(), "CRTC_ID");
			mPlaneFbPropId = prop_id(mDrmFd, pp.get(), "FB_ID"); mPlaneCrtcPropId = prop_id(mDrmFd, pp.get(), "CRTC_ID");
			mPlaneCrtcWPropId = prop_id(mDrmFd, pp.get(), "CRTC_W"); mPlaneCrtcHPropId = prop_id(mDrmFd, pp.get(), "CRTC_H");
			mPlaneSrcWPropId = prop_id(mDrmFd, pp.get(), "SRC_W"); mPlaneSrcHPropId = prop_id(mDrmFd, pp.get(), "SRC_H");
			mPrimaryFbPropId = prop_id(mDrmFd, primaryProps.get(), "FB_ID");
			mPrimaryCrtcPropId = prop_id(mDrmFd, primaryProps.get(), "CRTC_ID");
			mPrimaryCrtcWPropId = prop_id(mDrmFd, primaryProps.get(), "CRTC_W");
			mPrimaryCrtcHPropId = prop_id(mDrmFd, primaryProps.get(), "CRTC_H");
			mPrimarySrcWPropId = prop_id(mDrmFd, primaryProps.get(), "SRC_W");
			mPrimarySrcHPropId = prop_id(mDrmFd, primaryProps.get(), "SRC_H");
			if(!mModePropId || !mActivePropId || !mConnectorCrtcPropId || !mPlaneFbPropId || !mPlaneCrtcPropId ||
				!mPlaneCrtcWPropId || !mPlaneCrtcHPropId || !mPlaneSrcWPropId || !mPlaneSrcHPropId ||
				!mPrimaryFbPropId || !mPrimaryCrtcPropId || !mPrimaryCrtcWPropId || !mPrimaryCrtcHPropId ||
				!mPrimarySrcWPropId || !mPrimarySrcHPropId) {
				LOGE("TestCase2: required DRM atomic property missing"); cleanup_drm(); return false;
			}
			if(drmModeCreatePropertyBlob(mDrmFd, &mode, sizeof(mode), &mModeBlobId)) {
				LOGE("TestCase2: CreatePropertyBlob failed: %s", strerror(errno)); cleanup_drm(); return false;
			}
			std::shared_ptr<drmModeAtomicReq> req(drmModeAtomicAlloc(), drmModeAtomicFree);
			bool ok = req && add_prop(req.get(), mConnectorId, mConnectorCrtcPropId, "CRTC_ID", mCrtcId);
			ok &= req && add_prop(req.get(), mCrtcId, mModePropId, "MODE_ID", mModeBlobId);
			ok &= req && add_prop(req.get(), mCrtcId, mActivePropId, "ACTIVE", 1);
			ok &= req && add_prop(req.get(), mPrimaryPlaneId, mPrimaryCrtcPropId, "CRTC_ID", mCrtcId);
			ok &= req && add_prop(req.get(), mPrimaryPlaneId, mPrimaryFbPropId, "FB_ID", mPrimaryFbId);
			ok &= req && add_prop(req.get(), mPrimaryPlaneId, mPrimaryCrtcWPropId, "CRTC_W", width);
			ok &= req && add_prop(req.get(), mPrimaryPlaneId, mPrimaryCrtcHPropId, "CRTC_H", height);
			ok &= req && add_prop(req.get(), mPrimaryPlaneId, mPrimarySrcWPropId, "SRC_W", (uint64_t)width << 16);
			ok &= req && add_prop(req.get(), mPrimaryPlaneId, mPrimarySrcHPropId, "SRC_H", (uint64_t)height << 16);
			ok &= req && add_prop(req.get(), mPlaneId, mPlaneCrtcPropId, "CRTC_ID", mCrtcId);
			ok &= req && add_prop(req.get(), mPlaneId, mPlaneFbPropId, "FB_ID", mFbId);
			ok &= req && add_prop(req.get(), mPlaneId, mPlaneCrtcWPropId, "CRTC_W", width);
			ok &= req && add_prop(req.get(), mPlaneId, mPlaneCrtcHPropId, "CRTC_H", height);
			ok &= req && add_prop(req.get(), mPlaneId, mPlaneSrcWPropId, "SRC_W", (uint64_t)width << 16);
			ok &= req && add_prop(req.get(), mPlaneId, mPlaneSrcHPropId, "SRC_H", (uint64_t)height << 16);
			if(!ok || drmModeAtomicCommit(mDrmFd, req.get(), DRM_MODE_ATOMIC_ALLOW_MODESET, NULL) < 0) {
				LOGE("TestCase2: atomic DRM plane setup failed: %s", strerror(errno)); cleanup_drm(); return false;
			}
			LOGI("TestCase2: primary plane %u and video plane %u set once, 3840x2160@60 NV12 -> HDMI RGB",
				mPrimaryPlaneId, mPlaneId);
			return true;
		}

		void cleanup_drm() {
			if(mDrmFd < 0) return;
			if(mFbId && mPlaneCrtcPropId && mConnectorCrtcPropId) {
				std::shared_ptr<drmModeAtomicReq> req(drmModeAtomicAlloc(), drmModeAtomicFree);
				if(req) {
					add_prop(req.get(), mPrimaryPlaneId, mPrimaryFbPropId, "FB_ID", 0); add_prop(req.get(), mPrimaryPlaneId, mPrimaryCrtcPropId, "CRTC_ID", 0);
					add_prop(req.get(), mPrimaryPlaneId, mPrimaryCrtcWPropId, "CRTC_W", 0); add_prop(req.get(), mPrimaryPlaneId, mPrimaryCrtcHPropId, "CRTC_H", 0);
					add_prop(req.get(), mPrimaryPlaneId, mPrimarySrcWPropId, "SRC_W", 0); add_prop(req.get(), mPrimaryPlaneId, mPrimarySrcHPropId, "SRC_H", 0);
					add_prop(req.get(), mPlaneId, mPlaneFbPropId, "FB_ID", 0); add_prop(req.get(), mPlaneId, mPlaneCrtcPropId, "CRTC_ID", 0);
					add_prop(req.get(), mPlaneId, mPlaneCrtcWPropId, "CRTC_W", 0); add_prop(req.get(), mPlaneId, mPlaneCrtcHPropId, "CRTC_H", 0);
					add_prop(req.get(), mPlaneId, mPlaneSrcWPropId, "SRC_W", 0); add_prop(req.get(), mPlaneId, mPlaneSrcHPropId, "SRC_H", 0);
					add_prop(req.get(), mConnectorId, mConnectorCrtcPropId, "CRTC_ID", 0); add_prop(req.get(), mCrtcId, mModePropId, "MODE_ID", 0); add_prop(req.get(), mCrtcId, mActivePropId, "ACTIVE", 0);
					drmModeAtomicCommit(mDrmFd, req.get(), DRM_MODE_ATOMIC_ALLOW_MODESET, NULL);
				}
			}
			if(mDumbMap != MAP_FAILED && mDumbMap) { munmap(mDumbMap, mDumbSize); mDumbMap = MAP_FAILED; }
			if(mPrimaryDumbMap != MAP_FAILED && mPrimaryDumbMap) { munmap(mPrimaryDumbMap, mPrimaryDumbSize); mPrimaryDumbMap = MAP_FAILED; }
			if(mFbId) { drmModeRmFB(mDrmFd, mFbId); mFbId = 0; }
			if(mPrimaryFbId) { drmModeRmFB(mDrmFd, mPrimaryFbId); mPrimaryFbId = 0; }
			if(mBoHandle) { struct drm_mode_destroy_dumb d = {}; d.handle = mBoHandle; drmIoctl(mDrmFd, DRM_IOCTL_MODE_DESTROY_DUMB, &d); mBoHandle = 0; }
			if(mPrimaryBoHandle) { struct drm_mode_destroy_dumb d = {}; d.handle = mPrimaryBoHandle; drmIoctl(mDrmFd, DRM_IOCTL_MODE_DESTROY_DUMB, &d); mPrimaryBoHandle = 0; }
			if(mModeBlobId) { drmModeDestroyPropertyBlob(mDrmFd, mModeBlobId); mModeBlobId = 0; }
			drmClose(mDrmFd); mDrmFd = -1;
		}

		QRESULT start_audio_test_pattern() {
			QRESULT qres = QCAP_RS_SUCCESSFUL;
			const ULONG nChannels = 2;
			const ULONG nBits = 16;
			const ULONG nRate = 48000;

			qcap2_event_t* audioEvent = NULL;
			qres = NewEvent(_FreeStack_evt_, &audioEvent);
			if(qres != QCAP_RS_SUCCESSFUL) return qres;

			mAudioSource = qcap2_audio_source_new();
			if(! mAudioSource) return QCAP_RS_ERROR_OUT_OF_MEMORY;
			_FreeStack_evt_ += [this]() {
				qcap2_audio_source_delete(mAudioSource);
				mAudioSource = NULL;
			};
			qcap2_audio_source_set_backend_type(mAudioSource,
				QCAP2_AUDIO_SOURCE_BACKEND_TYPE_TPG);
			qcap2_audio_source_set_frame_count(mAudioSource, 4);
			qcap2_audio_source_set_test_pattern(mAudioSource,
				QCAP2_AUDIO_TEST_PATTERN_SINE_1KHZ_MINUS_20DBFS);
			qcap2_audio_source_set_event(mAudioSource, audioEvent);

			std::shared_ptr<qcap2_audio_format_t> audioFormat(
				qcap2_audio_format_new(), qcap2_audio_format_delete);
			if(! audioFormat) return QCAP_RS_ERROR_OUT_OF_MEMORY;
			qcap2_audio_format_set_property(audioFormat.get(), nChannels, nBits, nRate);
			qcap2_audio_source_set_audio_format(mAudioSource, audioFormat.get());

			mAudioSink = qcap2_audio_sink_new();
			if(! mAudioSink) return QCAP_RS_ERROR_OUT_OF_MEMORY;
			_FreeStack_evt_ += [this]() {
				qcap2_audio_sink_delete(mAudioSink);
				mAudioSink = NULL;
			};
			qcap2_audio_sink_set_backend_type(mAudioSink,
				QCAP2_AUDIO_SINK_BACKEND_TYPE_ALSA);
			qcap2_audio_sink_set_card(mAudioSink, 1);
			qcap2_audio_sink_set_device(mAudioSink, 0);
			qcap2_audio_sink_set_audio_format(mAudioSink, audioFormat.get());
			qcap2_audio_sink_set_period_time(mAudioSink, 20 * 1000);
			qcap2_audio_sink_set_buffer_time(mAudioSink, 8 * 20 * 1000);

			qres = qcap2_audio_sink_start(mAudioSink);
			if(qres != QCAP_RS_SUCCESSFUL) {
				LOGE("TestCase2: qcap2_audio_sink_start() failed, qres=%d", qres);
				return qres;
			}
			_FreeStack_evt_ += [this]() {
				QRESULT stopResult = qcap2_audio_sink_stop(mAudioSink);
				if(stopResult != QCAP_RS_SUCCESSFUL)
					LOGW("TestCase2: qcap2_audio_sink_stop() failed, qres=%d", stopResult);
			};

			qres = qcap2_audio_source_start(mAudioSource);
			if(qres != QCAP_RS_SUCCESSFUL) {
				LOGE("TestCase2: qcap2_audio_source_start() failed, qres=%d", qres);
				return qres;
			}
			_FreeStack_evt_ += [this]() {
				QRESULT stopResult = qcap2_audio_source_stop(mAudioSource);
				if(stopResult != QCAP_RS_SUCCESSFUL)
					LOGW("TestCase2: qcap2_audio_source_stop() failed, qres=%d", stopResult);
			};

			qres = AddEventHandler(_FreeStack_evt_, audioEvent,
				std::bind(&self_t::OnAudio, this));
			if(qres != QCAP_RS_SUCCESSFUL) return qres;
			LOGI("TestCase2: qcap2 ALSA test pattern started (48 kHz/16-bit/2-ch)");
			return QCAP_RS_SUCCESSFUL;
		}

		QRETURN OnAudio() {
			qcap2_rcbuffer_t* buffer = NULL;
			QRESULT qres = qcap2_audio_source_pop(mAudioSource, &buffer);
			if(qres != QCAP_RS_SUCCESSFUL) {
				LOGE("TestCase2: qcap2_audio_source_pop() failed, qres=%d", qres);
				return QCAP_RT_OK;
			}
			std::shared_ptr<qcap2_rcbuffer_t> bufferGuard(buffer, qcap2_rcbuffer_release);
			qres = qcap2_audio_sink_push(mAudioSink, buffer);
			if(qres != QCAP_RS_SUCCESSFUL)
				LOGE("TestCase2: qcap2_audio_sink_push() failed, qres=%d", qres);
			return QCAP_RT_OK;
		}

		void DoWork() {
			QRESULT qres = QCAP_RS_SUCCESSFUL;
			LOGD("%s::%s", typeid(self_t).name(), __FUNCTION__);
			init_state();

			qres = StartEventHandlers();
			if(qres == QCAP_RS_SUCCESSFUL) {
				ZzUtils::Scoped ZZ_GUARD_NAME([&]() {
					OnExitEventHandlers();
				});

				bool ok = AudioSwitch::select_source(AUDIO_MODE_TEST_PATTERN);
				if(ok) ok = setup_drm();
				if(! ok) qres = QCAP_RS_ERROR_GENERAL;
				if(ok) {
					QRESULT audioResult = QCAP_RS_SUCCESSFUL;
					qres = ExecInEventHandlers([&]() -> QRETURN {
						audioResult = start_audio_test_pattern();
						return QCAP_RT_OK;
					});
					ok = qres == QCAP_RS_SUCCESSFUL &&
						audioResult == QCAP_RS_SUCCESSFUL;
					if(! ok) qres = audioResult;
				}
				if(ok) {
					LOGI("TestCase2 is running; press 'q' to stop");
					wait_for_test_finish([&](int ch) -> bool {
						(void)ch;
						return true;
					}, 1000000LL, 10LL);
				}
			} else {
				LOGE("TestCase2: StartEventHandlers() failed, qres=%d", qres);
			}

			_FreeStack_main_.flush();
			cleanup_drm();
			if(qres != QCAP_RS_SUCCESSFUL) LOGE("TestCase2 failed, qres=%d", qres);
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
