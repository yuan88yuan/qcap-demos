#include <errno.h>
#include <fcntl.h>
#include <initializer_list>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include <drm.h>
#include <drm_fourcc.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

struct dumb_fb_t {
	uint32_t handle = 0;
	uint32_t fb_id = 0;
	uint32_t pitch = 0;
	uint64_t size = 0;
	uint64_t map_offset = 0;
};

struct drm_path_t {
	int fd = -1;
	uint32_t connector_id = 0;
	uint32_t crtc_id = 0;
	uint32_t primary_plane_id = 0;
	uint32_t video_plane_id = 0;
	drmModeModeInfo mode = {};
	uint32_t mode_blob_id = 0;
};

enum overlay_source_t : uint64_t {
	OVERLAY_SOURCE_DUMMY = 0,
	OVERLAY_SOURCE_DANTE = 1,
	OVERLAY_SOURCE_VCU = 2,
};

static uint32_t property_id(int fd, uint32_t object_id, uint32_t object_type,
	const char* name) {
	drmModeObjectProperties* properties = drmModeObjectGetProperties(fd, object_id, object_type);
	if (!properties) return 0;
	uint32_t id = 0;
	for (uint32_t i = 0; i < properties->count_props; ++i) {
		drmModePropertyRes* property = drmModeGetProperty(fd, properties->props[i]);
		if (property) {
			if (strcmp(property->name, name) == 0) id = property->prop_id;
			drmModeFreeProperty(property);
		}
		if (id) break;
	}
	drmModeFreeObjectProperties(properties);
	return id;
}

static bool is_primary_plane(int fd, uint32_t plane_id) {
	uint32_t type_id = property_id(fd, plane_id, DRM_MODE_OBJECT_PLANE, "type");
	drmModeObjectProperties* properties = drmModeObjectGetProperties(fd, plane_id, DRM_MODE_OBJECT_PLANE);
	if (!type_id || !properties) {
		if (properties) drmModeFreeObjectProperties(properties);
		return false;
	}
	bool primary = false;
	for (uint32_t i = 0; i < properties->count_props; ++i) {
		if (properties->props[i] == type_id && properties->prop_values[i] == DRM_PLANE_TYPE_PRIMARY) {
			primary = true;
			break;
		}
	}
	drmModeFreeObjectProperties(properties);
	return primary;
}

static bool plane_supports_format(const drmModePlane* plane, uint32_t format) {
	if (!plane) return false;
	for (uint32_t i = 0; i < plane->count_formats; ++i)
		if (plane->formats[i] == format) return true;
	return false;
}

static int create_dumb_fb(int fd, uint32_t width, uint32_t height, uint32_t bpp,
	uint32_t format, bool nv12, dumb_fb_t* fb) {
	drm_mode_create_dumb create = {};
	create.width = width;
	create.height = nv12 ? height * 3 / 2 : height;
	create.bpp = bpp;
	if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &create) != 0) return -1;
	fb->handle = create.handle;
	fb->pitch = create.pitch;
	fb->size = create.size;
	uint32_t handles[4] = { create.handle, create.handle, 0, 0 };
	uint32_t pitches[4] = { create.pitch, create.pitch, 0, 0 };
	uint32_t offsets[4] = { 0, nv12 ? create.pitch * height : 0, 0, 0 };
	if (drmModeAddFB2(fd, width, height, format, handles, pitches, offsets, &fb->fb_id, 0) != 0) {
		drm_mode_destroy_dumb destroy = {};
		destroy.handle = create.handle;
		drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
		memset(fb, 0, sizeof(*fb));
		return -1;
	}
	drm_mode_map_dumb map = {};
	map.handle = create.handle;
	if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map) != 0) {
		drmModeRmFB(fd, fb->fb_id);
		drm_mode_destroy_dumb destroy = {};
		destroy.handle = create.handle;
		drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
		memset(fb, 0, sizeof(*fb));
		return -1;
	}
	fb->map_offset = map.offset;
	return 0;
}

static void destroy_dumb_fb(int fd, dumb_fb_t* fb) {
	if (fb->fb_id) drmModeRmFB(fd, fb->fb_id);
	if (fb->handle) {
		drm_mode_destroy_dumb destroy = {};
		destroy.handle = fb->handle;
		drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
	}
	memset(fb, 0, sizeof(*fb));
}

static int fill_nv12_fb(int fd, const dumb_fb_t* fb, uint32_t width, uint32_t height,
	uint8_t y, uint8_t u, uint8_t v) {
	void* mapping = mmap(nullptr, fb->size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, fb->map_offset);
	if (mapping == MAP_FAILED) return -1;
	uint8_t* pixels = static_cast<uint8_t*>(mapping);
	for (uint32_t row = 0; row < height; ++row)
		memset(pixels + row * fb->pitch, y, width);
	uint8_t* uv = pixels + fb->pitch * height;
	for (uint32_t row = 0; row < height / 2; ++row) {
		for (uint32_t column = 0; column < width; column += 2) {
			uv[row * fb->pitch + column] = u;
			uv[row * fb->pitch + column + 1] = v;
		}
	}
	munmap(mapping, fb->size);
	return 0;
}

static int add_plane(drmModeAtomicReq* request, int fd, uint32_t plane_id,
	uint32_t crtc_id, uint32_t fb_id, uint32_t width, uint32_t height) {
	const char* names[] = { "CRTC_ID", "FB_ID", "CRTC_X", "CRTC_Y", "CRTC_W", "CRTC_H",
		"SRC_X", "SRC_Y", "SRC_W", "SRC_H" };
	uint64_t values[] = { crtc_id, fb_id, 0, 0, width, height, 0, 0,
		(uint64_t)width << 16, (uint64_t)height << 16 };
	for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
		uint32_t id = property_id(fd, plane_id, DRM_MODE_OBJECT_PLANE, names[i]);
		if (!id || drmModeAtomicAddProperty(request, plane_id, id, values[i]) < 0) return -1;
	}
	return 0;
}

static int add_overlay_source(drmModeAtomicReq* request, int fd, uint32_t plane_id,
		overlay_source_t source) {
	uint32_t id = property_id(fd, plane_id, DRM_MODE_OBJECT_PLANE,
		"SC6F0_OVERLAY_SOURCE");
	if (!id) {
		errno = ENOENT;
		return -1;
	}
	return drmModeAtomicAddProperty(request, plane_id, id, source) < 0 ? -1 : 0;
}

static int select_path(drm_path_t* path, uint32_t width, uint32_t height, uint32_t fps,
	uint32_t pixel_clock_khz, bool with_layer2) {
	/* This platform DRM device has no libdrm driver-name mapping. */
	path->fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
	if (path->fd < 0 || drmSetClientCap(path->fd, DRM_CLIENT_CAP_ATOMIC, 1) ||
		drmSetClientCap(path->fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1)) return -1;
	drmModeRes* resources = drmModeGetResources(path->fd);
	drmModePlaneRes* planes = nullptr;
	int crtc_index = -1;
	if (!resources) goto fail;
	for (int i = 0; i < resources->count_connectors && !path->crtc_id; ++i) {
		drmModeConnector* connector = drmModeGetConnector(path->fd, resources->connectors[i]);
		if (!connector || connector->connection != DRM_MODE_CONNECTED) {
			if (connector) drmModeFreeConnector(connector);
			continue;
		}
		for (int m = 0; m < connector->count_modes; ++m) {
			if (connector->modes[m].hdisplay != width || connector->modes[m].vdisplay != height ||
				connector->modes[m].vrefresh != fps || connector->modes[m].clock != pixel_clock_khz) continue;
			uint32_t encoder_id = connector->encoder_id ? connector->encoder_id : connector->encoders[0];
			drmModeEncoder* encoder = drmModeGetEncoder(path->fd, encoder_id);
			if (encoder) {
				for (int c = 0; c < resources->count_crtcs; ++c) {
					if (encoder->possible_crtcs & (1u << c)) {
						path->connector_id = connector->connector_id;
						path->crtc_id = resources->crtcs[c];
						path->mode = connector->modes[m];
						crtc_index = c;
						break;
					}
				}
				drmModeFreeEncoder(encoder);
			}
			break;
		}
		drmModeFreeConnector(connector);
	}
	if (!path->crtc_id) goto fail;
	planes = drmModeGetPlaneResources(path->fd);
	if (!planes) goto fail;
	for (uint32_t i = 0; i < planes->count_planes; ++i) {
		drmModePlane* plane = drmModeGetPlane(path->fd, planes->planes[i]);
		if (plane && (plane->possible_crtcs & (1u << crtc_index)) &&
			is_primary_plane(path->fd, plane->plane_id) && plane_supports_format(plane, DRM_FORMAT_BGR888))
			path->primary_plane_id = plane->plane_id;
		if (plane && (plane->possible_crtcs & (1u << crtc_index)) &&
			!is_primary_plane(path->fd, plane->plane_id) && plane_supports_format(plane, DRM_FORMAT_NV12))
			path->video_plane_id = plane->plane_id;
		if (plane) drmModeFreePlane(plane);
	}
	drmModeFreePlaneResources(planes);
	drmModeFreeResources(resources);
	if (!path->primary_plane_id || (with_layer2 && !path->video_plane_id) ||
		drmModeCreatePropertyBlob(path->fd, &path->mode, sizeof(path->mode), &path->mode_blob_id)) goto fail_close;
	return 0;

fail:
	if (planes) drmModeFreePlaneResources(planes);
	if (resources) drmModeFreeResources(resources);
fail_close:
	if (path->fd >= 0) drmClose(path->fd);
	path->fd = -1;
	return -1;
}

static void close_path(drm_path_t* path) {
	if (path->mode_blob_id) drmModeDestroyPropertyBlob(path->fd, path->mode_blob_id);
	if (path->fd >= 0) drmClose(path->fd);
	path->fd = -1;
}

static double monotonic_seconds() {
	struct timespec ts = {};
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1000000000.0;
}

/* Measure the DRM core's vblank sequence directly.  Unlike the NV12 flip
 * loop this neither changes a plane nor reconfigures VProcSS/DMA. */
static int measure_vblank_rate(int fd, uint32_t samples) {
	if (!samples) return 0;
	drmVBlank vblank = {};
	double start = monotonic_seconds();
	for (uint32_t i = 0; i < samples; ++i) {
		vblank.request.type = DRM_VBLANK_RELATIVE;
		vblank.request.sequence = 1;
		if (drmWaitVBlank(fd, &vblank) != 0) {
			fprintf(stderr, "vblank wait %u failed: %s\n", i, strerror(errno));
			return -1;
		}
	}
	double elapsed = monotonic_seconds() - start;
	if (elapsed <= 0.0) return -1;
	printf("DRM vblank: %u samples in %.3f s = %.2f Hz (%.3f ms/event)\n",
		samples, elapsed, samples / elapsed, elapsed * 1000.0 / samples);
	return 0;
}

struct flip_event_t { bool done = false; };

static void on_page_flip(int, unsigned int, unsigned int, unsigned int, void* data) {
	((flip_event_t*)data)->done = true;
}

static int wait_page_flip(int fd, flip_event_t* event) {
	struct pollfd pollfd = {};
	pollfd.fd = fd;
	pollfd.events = POLLIN;
	if (poll(&pollfd, 1, 1000) <= 0) return -1;
	drmEventContext context = {};
	context.version = DRM_EVENT_CONTEXT_VERSION;
	context.page_flip_handler = on_page_flip;
	if (drmHandleEvent(fd, &context) != 0 || !event->done) return -1;
	return 0;
}

static int configure_tpg_color_bars() {
	constexpr const char* path = "/sys/bus/platform/devices/a0000000.v_tpg/bckgnd_id";
	int fd = open(path, O_WRONLY);
	if (fd < 0) return -1;
	/* XTPG_BKGND_COLOR_BARS is pattern ID 9 in xv_tpg.h. */
	const char value[] = "9\n";
	ssize_t written = write(fd, value, sizeof(value) - 1);
	int saved_errno = errno;
	close(fd);
	if (written != (ssize_t)(sizeof(value) - 1)) {
		errno = saved_errno;
		return -1;
	}
	return 0;
}

int main(int argc, char** argv) {
	constexpr uint32_t width = 3840, height = 2160, fps = 60;
	constexpr uint32_t pixel_clock_khz = 594000; /* CEA-861 2160p60 timing */
	uint32_t frames = 600;
	uint32_t seconds = 30;
	uint32_t source_cycles = 0;
	uint32_t source_hold_seconds = 0;
	uint32_t primary_cycles = 0;
	uint32_t hold_seconds = 0;
	uint32_t vblank_samples = 0;
	bool vblank_only = false;
	bool with_layer2 = false;
	overlay_source_t overlay_source = OVERLAY_SOURCE_VCU;
	for (int i = 1; i < argc; ++i) {
		if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) frames = strtoul(argv[++i], nullptr, 10);
		else if (strcmp(argv[i], "--seconds") == 0 && i + 1 < argc) seconds = strtoul(argv[++i], nullptr, 10);
		else if (strcmp(argv[i], "--source-cycles") == 0 && i + 1 < argc) source_cycles = strtoul(argv[++i], nullptr, 10);
		else if (strcmp(argv[i], "--source-hold-seconds") == 0 && i + 1 < argc) source_hold_seconds = strtoul(argv[++i], nullptr, 10);
		else if (strcmp(argv[i], "--primary-cycles") == 0 && i + 1 < argc) primary_cycles = strtoul(argv[++i], nullptr, 10);
		else if (strcmp(argv[i], "--hold-seconds") == 0 && i + 1 < argc) hold_seconds = strtoul(argv[++i], nullptr, 10);
		else if (strcmp(argv[i], "--vblank-samples") == 0 && i + 1 < argc) vblank_samples = strtoul(argv[++i], nullptr, 10);
		else if (strcmp(argv[i], "--vblank-only") == 0) vblank_only = true;
		else if (strcmp(argv[i], "--layer2") == 0) with_layer2 = true;
		else if (strcmp(argv[i], "--overlay-source") == 0 && i + 1 < argc) {
			const char* value = argv[++i];
			if (strcmp(value, "vcu") == 0) overlay_source = OVERLAY_SOURCE_VCU;
			else if (strcmp(value, "dante") == 0) overlay_source = OVERLAY_SOURCE_DANTE;
			else if (strcmp(value, "dummy") == 0) overlay_source = OVERLAY_SOURCE_DUMMY;
			else return 2;
		}
		else if (strcmp(argv[i], "--help") == 0) {
			printf("Usage: %s [--seconds n] [--layer2 --frames n] [--overlay-source vcu|dante|dummy] [--source-cycles n] [--source-hold-seconds n] [--primary-cycles n] [--hold-seconds n] [--vblank-samples n] [--vblank-only]\n", argv[0]);
			printf("Default: primary plane only, fixed 3840x2160@60 and TPG color bars for 30 seconds.\n");
			printf("--layer2 additionally enables the NV12 overlay and flips its buffers; source defaults to VCU.\n");
			printf("--vblank-only measures the active CRTC without modifying its modeset or planes.\n");
			return 0;
		} else return 2;
	}
	if (vblank_only) {
		if (!vblank_samples) {
			fprintf(stderr, "--vblank-only requires --vblank-samples n\n");
			return 2;
		}
		int fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
		if (fd < 0) {
			perror("Cannot open DRM device");
			return 1;
		}
		int result = measure_vblank_rate(fd, vblank_samples);
		close(fd);
		return result == 0 ? 0 : 1;
	}
	drm_path_t path;
	if (select_path(&path, width, height, fps, pixel_clock_khz, with_layer2) != 0) {
		fprintf(stderr, "Cannot select xlnx DRM connector/CRTC%s: %s\n",
			with_layer2 ? "/layer-2 plane 36" : "", strerror(errno));
		return 1;
	}
	dumb_fb_t primary = {}, video[2] = {};
	if (create_dumb_fb(path.fd, width, height, 24, DRM_FORMAT_BGR888, false, &primary) ||
		(with_layer2 && (create_dumb_fb(path.fd, width, height, 8, DRM_FORMAT_NV12, true, &video[0]) ||
			create_dumb_fb(path.fd, width, height, 8, DRM_FORMAT_NV12, true, &video[1]) ||
			fill_nv12_fb(path.fd, &video[0], width, height, 76, 84, 255) ||
			fill_nv12_fb(path.fd, &video[1], width, height, 150, 44, 21)))) {
		fprintf(stderr, "Cannot allocate DRM test framebuffers: %s\n", strerror(errno));
		destroy_dumb_fb(path.fd, &video[1]); destroy_dumb_fb(path.fd, &video[0]); destroy_dumb_fb(path.fd, &primary); close_path(&path);
		return 1;
	}
	drmModeAtomicReq* request = drmModeAtomicAlloc();
	uint32_t connector_crtc = property_id(path.fd, path.connector_id, DRM_MODE_OBJECT_CONNECTOR, "CRTC_ID");
	uint32_t connector_dpms = property_id(path.fd, path.connector_id, DRM_MODE_OBJECT_CONNECTOR, "DPMS");
	uint32_t crtc_mode = property_id(path.fd, path.crtc_id, DRM_MODE_OBJECT_CRTC, "MODE_ID");
	uint32_t crtc_active = property_id(path.fd, path.crtc_id, DRM_MODE_OBJECT_CRTC, "ACTIVE");
	/* Xilinx exposes DPMS as a legacy (non-atomic) connector property. */
	if (!request || !connector_crtc || !connector_dpms || !crtc_mode || !crtc_active ||
		drmModeConnectorSetProperty(path.fd, path.connector_id, connector_dpms, DRM_MODE_DPMS_ON) != 0 ||
		drmModeAtomicAddProperty(request, path.connector_id, connector_crtc, path.crtc_id) < 0 ||
		drmModeAtomicAddProperty(request, path.crtc_id, crtc_mode, path.mode_blob_id) < 0 ||
		drmModeAtomicAddProperty(request, path.crtc_id, crtc_active, 1) < 0 ||
		add_plane(request, path.fd, path.primary_plane_id, path.crtc_id, primary.fb_id, width, height) ||
		(with_layer2 && add_plane(request, path.fd, path.video_plane_id, path.crtc_id, video[0].fb_id, width, height)) ||
		(with_layer2 && add_overlay_source(request, path.fd, path.video_plane_id, overlay_source)) ||
		drmModeAtomicCommit(path.fd, request, DRM_MODE_ATOMIC_ALLOW_MODESET, nullptr) != 0) {
		fprintf(stderr, "Initial mixer modeset failed: %s\n", strerror(errno));
		drmModeAtomicFree(request); destroy_dumb_fb(path.fd, &video[1]); destroy_dumb_fb(path.fd, &video[0]); destroy_dumb_fb(path.fd, &primary); close_path(&path);
		return 1;
	}
	drmModeAtomicFree(request);
	/*
	 * Start the master TPG only after the initial KMS modeset.  This matches
	 * the working Dante RX setup sequence and avoids a frame-done interrupt
	 * arriving while the first atomic commit is being installed.
	 */
	if (configure_tpg_color_bars() != 0) {
		fprintf(stderr, "Cannot select TPG color-bars pattern: %s\n", strerror(errno));
		destroy_dumb_fb(path.fd, &video[1]); destroy_dumb_fb(path.fd, &video[0]);
		destroy_dumb_fb(path.fd, &primary); close_path(&path);
		return 1;
	}
	if (measure_vblank_rate(path.fd, vblank_samples) != 0) {
		destroy_dumb_fb(path.fd, &video[1]); destroy_dumb_fb(path.fd, &video[0]);
		destroy_dumb_fb(path.fd, &primary); close_path(&path);
		return 1;
	}
	if ((source_cycles || primary_cycles) && !with_layer2) {
		fprintf(stderr, "--source-cycles and --primary-cycles require --layer2\n");
		destroy_dumb_fb(path.fd, &video[1]); destroy_dumb_fb(path.fd, &video[0]);
		destroy_dumb_fb(path.fd, &primary); close_path(&path);
		return 2;
	}
	if (with_layer2 && hold_seconds) {
		/* Keep this same CRTC session active long enough for a 4K HDMI monitor
		 * to complete link training before source/plane stress begins. */
		printf("Holding active CRTC and layer 2 for %u seconds before transitions\n",
			hold_seconds);
		sleep(hold_seconds);
	}
	if (with_layer2 && source_cycles) {
		uint32_t source_prop = property_id(path.fd, path.video_plane_id,
			DRM_MODE_OBJECT_PLANE, "SC6F0_OVERLAY_SOURCE");
		if (!source_prop) {
			fprintf(stderr, "Cannot find SC6F0_OVERLAY_SOURCE property\n");
			return 1;
		}
		printf("Switching DUMMY <-> VCU %u times with CRTC active\n", source_cycles);
		for (uint32_t i = 0; i < source_cycles; ++i) {
			for (overlay_source_t source : { OVERLAY_SOURCE_DUMMY, OVERLAY_SOURCE_VCU }) {
				request = drmModeAtomicAlloc();
				if (!request || drmModeAtomicAddProperty(request, path.video_plane_id,
					source_prop, source) < 0 ||
					drmModeAtomicCommit(path.fd, request, 0, nullptr) != 0) {
					fprintf(stderr, "Source switch %u failed: %s\n", i, strerror(errno));
					if (request) drmModeAtomicFree(request);
					return 1;
				}
				drmModeAtomicFree(request);
				if (source_hold_seconds) {
					printf("  Cycle %u: %s for %u seconds\n", i + 1,
						source == OVERLAY_SOURCE_DUMMY ? "DUMMY" : "VCU/DDR",
						source_hold_seconds);
					fflush(stdout);
					if (measure_vblank_rate(path.fd, source_hold_seconds * fps) != 0)
						return 1;
				}
			}
		}
	}
	if (with_layer2 && primary_cycles) {
		uint32_t primary_crtc = property_id(path.fd, path.primary_plane_id,
			DRM_MODE_OBJECT_PLANE, "CRTC_ID");
		uint32_t primary_fb = property_id(path.fd, path.primary_plane_id,
			DRM_MODE_OBJECT_PLANE, "FB_ID");
		if (!primary_crtc || !primary_fb) {
			fprintf(stderr, "Cannot find primary-plane properties\n");
			return 1;
		}
		printf("Toggling primary plane %u times with CRTC/layer 2 active\n", primary_cycles);
		for (uint32_t i = 0; i < primary_cycles; ++i) {
			request = drmModeAtomicAlloc();
			if (!request ||
				drmModeAtomicAddProperty(request, path.primary_plane_id, primary_fb, 0) < 0 ||
				drmModeAtomicAddProperty(request, path.primary_plane_id, primary_crtc, 0) < 0 ||
				drmModeAtomicCommit(path.fd, request, 0, nullptr) != 0) {
				fprintf(stderr, "Primary disable %u failed: %s\n", i, strerror(errno));
				if (request) drmModeAtomicFree(request);
				return 1;
			}
			drmModeAtomicFree(request);
			request = drmModeAtomicAlloc();
			if (!request || add_plane(request, path.fd, path.primary_plane_id,
				path.crtc_id, primary.fb_id, width, height) ||
				drmModeAtomicCommit(path.fd, request, 0, nullptr) != 0) {
				fprintf(stderr, "Primary enable %u failed: %s\n", i, strerror(errno));
				if (request) drmModeAtomicFree(request);
				return 1;
			}
			drmModeAtomicFree(request);
		}
	}
	if (!with_layer2) {
		printf("DRM TPG test: connector=%u CRTC=%u primary=%u, showing fixed color bars for %u seconds\n",
			path.connector_id, path.crtc_id, path.primary_plane_id, seconds);
		sleep(seconds);
	} else {
		printf("DRM mixer test: connector=%u CRTC=%u primary=%u video=%u source=%s, flipping %u NV12 buffers\n",
			path.connector_id, path.crtc_id, path.primary_plane_id, path.video_plane_id,
			overlay_source == OVERLAY_SOURCE_VCU ? "VCU" :
			overlay_source == OVERLAY_SOURCE_DANTE ? "DANTE" : "DUMMY", frames);
	}
	double start_time = monotonic_seconds();
	double interval_start = start_time;
	int status = 0;
	for (uint32_t i = 0; with_layer2 && i < frames; ++i) {
		request = drmModeAtomicAlloc();
		uint32_t fb_id = video[(i + 1) & 1].fb_id;
		uint32_t plane_fb = property_id(path.fd, path.video_plane_id, DRM_MODE_OBJECT_PLANE, "FB_ID");
		uint32_t src_w = property_id(path.fd, path.video_plane_id, DRM_MODE_OBJECT_PLANE, "SRC_W");
		uint32_t src_h = property_id(path.fd, path.video_plane_id, DRM_MODE_OBJECT_PLANE, "SRC_H");
		flip_event_t event;
		if (!request || !plane_fb || !src_w || !src_h ||
			drmModeAtomicAddProperty(request, path.video_plane_id, plane_fb, fb_id) < 0 ||
			drmModeAtomicAddProperty(request, path.video_plane_id, src_w, (uint64_t)width << 16) < 0 ||
			drmModeAtomicAddProperty(request, path.video_plane_id, src_h, (uint64_t)height << 16) < 0 ||
			drmModeAtomicCommit(path.fd, request,
				DRM_MODE_ATOMIC_ALLOW_MODESET | DRM_MODE_ATOMIC_NONBLOCK |
				DRM_MODE_PAGE_FLIP_EVENT, &event) != 0 ||
			wait_page_flip(path.fd, &event) != 0) {
			fprintf(stderr, "Flip %u failed: %s\n", i, strerror(errno));
			if (request) drmModeAtomicFree(request);
			status = 1;
			break;
		}
		drmModeAtomicFree(request);
		if ((i + 1) % fps == 0) {
			double now = monotonic_seconds();
			printf("  %u flips completed: %.3f s, %.2f flips/s (last %u: %.3f s)\n",
				i + 1, now - start_time, (i + 1) / (now - start_time), fps, now - interval_start);
			interval_start = now;
		}
	}
	if (with_layer2 && hold_seconds) {
		printf("Holding active CRTC and layer 2 for %u seconds after transitions\n",
			hold_seconds);
		sleep(hold_seconds);
	}
	printf("Disabling primary plane and CRTC%s...\n", with_layer2 ? ", and layer 2" : "");
	request = drmModeAtomicAlloc();
	uint32_t video_crtc = property_id(path.fd, path.video_plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_ID");
	uint32_t video_fb = property_id(path.fd, path.video_plane_id, DRM_MODE_OBJECT_PLANE, "FB_ID");
	uint32_t primary_crtc = property_id(path.fd, path.primary_plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_ID");
	uint32_t primary_fb = property_id(path.fd, path.primary_plane_id, DRM_MODE_OBJECT_PLANE, "FB_ID");
	if (request && primary_crtc && primary_fb &&
		(!with_layer2 || (video_crtc && video_fb))) {
		if (with_layer2) {
			drmModeAtomicAddProperty(request, path.video_plane_id, video_fb, 0);
			drmModeAtomicAddProperty(request, path.video_plane_id, video_crtc, 0);
		}
		drmModeAtomicAddProperty(request, path.primary_plane_id, primary_fb, 0);
		drmModeAtomicAddProperty(request, path.primary_plane_id, primary_crtc, 0);
		drmModeAtomicAddProperty(request, path.crtc_id, crtc_mode, 0);
		drmModeAtomicAddProperty(request, path.crtc_id, crtc_active, 0);
		drmModeAtomicAddProperty(request, path.connector_id, connector_crtc, 0);
		if (drmModeAtomicCommit(path.fd, request, DRM_MODE_ATOMIC_ALLOW_MODESET, nullptr) != 0)
			fprintf(stderr, "Detach failed: %s\n", strerror(errno));
	}
	if (request) drmModeAtomicFree(request);
	destroy_dumb_fb(path.fd, &video[1]); destroy_dumb_fb(path.fd, &video[0]); destroy_dumb_fb(path.fd, &primary); close_path(&path);
	return status;
}
