#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <limits.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <dirent.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <drm.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>

extern "C" {
#include "dauservice.h"
}

#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <vector>
#include <alsa/asoundlib.h>
#include <libavutil/pixfmt.h>
#include <libavutil/samplefmt.h>

#include "ZzLog.h"
#include "ZzModules.h"
#include "ZzClock.h"
#include "ZzUtils.h"
#include "testkit.h"

// zstreamer headers
#include "zst_types.h"
#include "zst_buffer.h"
#include "zst_pad.h"
#include "zst_element.h"
#include "zst_pipeline.h"
#include "zst_scheduler.h"
#include "zst_bus.h"
#include "zst_clock.h"
#include "zst_log.h"
#include "zstreamer/elements/zst_dante_session.h"
#include "zstreamer/elements/zst_dante_video_coordinator.h"
#include "zstreamer/elements/zst_dante_dep_audio.h"
#include "zstreamer/elements/zst_h26x_parser.h"
#include "zstreamer/elements/zst_audio_resampler.h"
#include "zstreamer/elements/zst_rtsp_source.h"
#include "zstreamer/elements/zst_h264_decoder.h"
#include "zstreamer/elements/zst_aac_parser.h"
#include "zstreamer/elements/zst_aac_decoder.h"

// qcap2 headers (ALLEGRO2 hardware decode)
#include <qcap2.h>
#include <qcap2.allegro2.h>
#include <qcap2.alsa.h>
#include <qcap2.drm.h>
#include <qcap2.user.h>
#include <qcap.common.h>

ZZ_INIT_LOG("sc6f0-dante-rx");

int g_argc = 0;
char** g_argv = NULL;

ZZ_MODULE_DECL(__zz_log__);

using namespace __zz_clock__;

namespace __sc6f0_dante_rx__ {
	ZZ_MODULES_INIT();
	struct modules_init_t {
		modules_init_t() {
			ZZ_MODULE_INIT(__zz_log__);
		}
		~modules_init_t() {
			ZZ_MODULES_UNINIT();
		}
	};

	static inline void set_str(zst_element_t* el, const char* p, const char* v) {
		zst_element_set_property(el, p, v);
	}

	static inline void set_uint(zst_element_t* el, const char* p, uint32_t v) {
		char buf[32];
		snprintf(buf, sizeof(buf), "%u", v);
		zst_element_set_property(el, p, buf);
	}

	static uint32_t get_uint_prop(zst_element_t* el, const char* p) {
		char buf[64] = {0};
		if (el && zst_element_get_property(el, p, buf, sizeof(buf)) == ZST_OK) {
			return (uint32_t)strtoul(buf, nullptr, 10);
		}
		return 0;
	}

	static uint32_t drm_property_id(int fd, uint32_t object_id, uint32_t object_type,
		const char* name) {
		std::shared_ptr<drmModeObjectProperties> properties(
			drmModeObjectGetProperties(fd, object_id, object_type), drmModeFreeObjectProperties);
		if (!properties) return 0;
		for (uint32_t i = 0; i < properties->count_props; ++i) {
			std::shared_ptr<drmModePropertyRes> property(
				drmModeGetProperty(fd, properties->props[i]), drmModeFreeProperty);
			if (property && strcmp(property->name, name) == 0)
				return property->prop_id;
		}
		return 0;
	}

	/* ── Video statistics & custom sink ──────────────────────────────── */
	struct zst_dante_rx_t; /* forward declaration */
	struct rx_video_stats_t {
		zst_dante_rx_t* rx = nullptr;
		std::atomic<uint64_t> total_packets{0};
		std::atomic<uint64_t> total_bytes{0};
		std::atomic<uint64_t> keyframes{0};
		std::atomic<uint64_t> sps_pps{0};
		std::atomic<uint64_t> idr_slices{0};
		std::atomic<uint64_t> non_idr_slices{0};
		std::atomic<uint64_t> no_sps_count{0};
		std::atomic<uint64_t> no_sc{0};
		std::atomic<uint64_t> nal_other{0};
		std::atomic<uint64_t> last_pts{0};
		std::atomic<uint64_t> first_pts{0};
	};

	/* Defined after the qcap2 decoder state. */
	static void rx_video_feed_decoder(rx_video_stats_t* stats,
		const uint8_t* data, size_t data_size, uint32_t flags, uint64_t pts);

	static zst_result_t rx_video_sink_push(zst_pad_t* pad, zst_buffer_t* buf) {
		if (!pad || !pad->parent || !buf) return ZST_ERROR;
		rx_video_stats_t* stats = (rx_video_stats_t*)pad->parent->priv;
		if (!stats) return ZST_ERROR;

		stats->total_packets++;
		stats->total_bytes += buf->memory.size;
		if (stats->first_pts == 0 && buf->pts > 0)
			stats->first_pts = buf->pts;
		stats->last_pts = buf->pts;

		const uint8_t* data = (const uint8_t*)buf->memory.data;
		size_t data_size = buf->memory.size;
		if (!data || data_size < 4)
			return ZST_OK;

		/* ── NAL classification (scan the full AU; an AUD often comes first) */
		{
			int found_nal = 0;
			int has_sps_pps = 0;
			int has_idr = 0;
			int has_non_idr = 0;
			size_t max_scan = data_size < 4096 ? data_size : 4096;
			for (size_t i = 0; i + 3 < max_scan; i++) {
				if (data[i] == 0 && data[i + 1] == 0 &&
				    (data[i + 2] == 1 ||
				     (i + 4 <= data_size && data[i + 2] == 0 && data[i + 3] == 1))) {
					size_t off = (data[i + 2] == 1) ? i + 3 : i + 4;
					if (off < data_size) {
						uint8_t nal_type = data[off] & 0x1F;
						found_nal = 1;
						if (nal_type == 7 || nal_type == 8) has_sps_pps = 1;
						else if (nal_type == 5) has_idr = 1;
						else if (nal_type == 1) has_non_idr = 1;
					}
				}
			}
			if (!found_nal) {
				stats->no_sc++;
			} else if (has_sps_pps) {
				stats->sps_pps++;
			}
			if (has_idr) {
				stats->idr_slices++;
				stats->keyframes++;
			} else if (buf->flags & ZST_BUFFER_FLAG_KEYFRAME) {
				stats->keyframes++;
			}
			if (has_non_idr) {
				stats->non_idr_slices++;
			} else if (!has_sps_pps && !has_idr) {
				stats->nal_other++;
			}
		}

		rx_video_feed_decoder(stats, data, data_size, buf->flags, buf->pts);

		return ZST_OK;
	}

	static zst_result_t rx_video_sink_open(zst_element_t* el) {
		(void)el;
		return ZST_OK;
	}

	static zst_element_t* create_rx_video_sink(rx_video_stats_t** pStatsOut) {
		static zst_element_ops_t ops = {};
		ops.name = "dante_rx_video_sink";
		ops.open = rx_video_sink_open;

		rx_video_stats_t* priv = (rx_video_stats_t*)calloc(1, sizeof(rx_video_stats_t));
		if (!priv) return nullptr;

		zst_element_t* el = zst_element_create(&ops, priv);
		if (!el) {
			free(priv);
			return nullptr;
		}

		zst_pad_t* sink_pad = zst_pad_create("sink", ZST_PAD_SINK);
		if (!sink_pad) {
			zst_element_destroy(el);
			return nullptr;
		}
		sink_pad->push = rx_video_sink_push;
		if (zst_element_add_pad(el, sink_pad) != ZST_OK) {
			zst_pad_destroy(sink_pad);
			zst_element_destroy(el);
			return nullptr;
		}
		if (pStatsOut) *pStatsOut = priv;
		return el;
	}

	/* ── Audio statistics & custom sink ──────────────────────────────── */
	struct zst_dante_rx_t;
	struct rx_audio_stats_t {
		zst_dante_rx_t* rx = nullptr;
		std::atomic<uint64_t> total_periods{0};
		std::atomic<uint64_t> total_samples{0};
		std::atomic<uint64_t> total_bytes{0};
		std::atomic<uint32_t> sample_rate{0};
		std::atomic<uint32_t> channels{0};
		std::atomic<uint32_t> format{0};
		std::atomic<uint64_t> last_pts{0};
		std::atomic<uint64_t> first_pts{0};
	};
	static void rx_audio_playback(zst_dante_rx_t* rx, const zst_buffer_t* buf,
		const zst_audio_frame_t* audio);

	static zst_result_t rx_audio_sink_push(zst_pad_t* pad, zst_buffer_t* buf) {
		if (!pad || !pad->parent || !buf) return ZST_ERROR;
		rx_audio_stats_t* stats = (rx_audio_stats_t*)pad->parent->priv;
		if (!stats) return ZST_ERROR;

		stats->total_periods++;
		stats->total_bytes += buf->memory.size;
		if (stats->first_pts == 0 && buf->pts > 0) {
			stats->first_pts = buf->pts;
		}
		stats->last_pts = buf->pts;

		if (buf->payload) {
			const zst_audio_frame_t* af = (const zst_audio_frame_t*)buf->payload;
			stats->total_samples += af->nb_samples;
			stats->sample_rate = af->sample_rate;
			stats->channels = af->channels;
			stats->format = af->format;
		} else {
			/* Standard DEP default: 2ch 32-bit (8 bytes/frame) */
			stats->total_samples += buf->memory.size / 8;
		}
		rx_audio_playback(stats->rx, buf,
			buf->payload ? (const zst_audio_frame_t*)buf->payload : nullptr);

		return ZST_OK;
	}

	static zst_result_t rx_audio_sink_open(zst_element_t* el) {
		(void)el;
		return ZST_OK;
	}

	static zst_element_t* create_rx_audio_sink(rx_audio_stats_t** pStatsOut) {
		static zst_element_ops_t ops = {};
		ops.name = "dante_rx_audio_sink";
		ops.open = rx_audio_sink_open;

		rx_audio_stats_t* priv = (rx_audio_stats_t*)calloc(1, sizeof(rx_audio_stats_t));
		if (!priv) return nullptr;

		zst_element_t* el = zst_element_create(&ops, priv);
		if (!el) {
			free(priv);
			return nullptr;
		}

		zst_pad_t* sink_pad = zst_pad_create("sink", ZST_PAD_SINK);
		if (!sink_pad) {
			zst_element_destroy(el);
			return nullptr;
		}
		sink_pad->push = rx_audio_sink_push;
		if (zst_element_add_pad(el, sink_pad) != ZST_OK) {
			zst_pad_destroy(sink_pad);
			zst_element_destroy(el);
			return nullptr;
		}
		if (pStatsOut) *pStatsOut = priv;
		return el;
	}

	/* ── QCAP Dante API alternative ─────────────────────────────────── */
	struct qcap_dante_rx_t {
		PVOID server = nullptr;
		PVOID client = nullptr;
		zst_dante_rx_t* media_rx = nullptr;
		bool server_started = false;
		bool client_started = false;
	};

	static zst_dante_rx_t* start_qcap_media_rx(bool display_enabled);
	static void stop_qcap_media_rx(zst_dante_rx_t* rx);
	static QRETURN qcap_dante_video_callback(PVOID client, UINT session,
		double sample_time, BYTE* stream, ULONG stream_len, BOOL is_keyframe, PVOID user_data);
	static QRETURN qcap_dante_audio_callback(PVOID client, UINT session,
		double sample_time, BYTE* stream, ULONG stream_len, PVOID user_data);
	static QRETURN qcap_dante_connected_callback(PVOID client, UINT session,
		ULONG video_format, ULONG width, ULONG height, BOOL interleaved, double fps,
		ULONG audio_format, ULONG audio_channels, ULONG audio_bits, ULONG audio_rate, PVOID user_data);
	static QRETURN qcap_dante_fail_callback(PVOID client, UINT session,
		QRESULT status, DWORD error_code, PVOID user_data);

	static QRETURN qcap_dante_message_callback(PVOID /*server*/, UINT video_channel,
	                                           DWORD flags, PVOID /*user_data*/) {
		const char* media = (flags & QCAP_DANTE_FLAG_VIDEO) ? "video" :
		                    (flags & QCAP_DANTE_FLAG_AUDIO) ? "audio" : "unknown-media";
		const char* direction = (flags & QCAP_DANTE_FLAG_RX) ? "RX" :
		                        (flags & QCAP_DANTE_FLAG_TX) ? "TX" : "unknown-direction";
		const char* transport = (flags & QCAP_DANTE_FLAG_UNICAST) ? "unicast" :
		                        (flags & QCAP_DANTE_FLAG_MULTICAST) ? "multicast" : "unknown-transport";
		const char* state = (flags & QCAP_DANTE_FLAG_CONNECTED) ? "connected" :
		                    (flags & QCAP_DANTE_FLAG_DISCONNECT) ? "disconnected" :
		                    (flags & QCAP_DANTE_FLAG_NO_VIDEO_PACKET) ? "no-video-packet" : "updated";
		LOGI("[QCAP DANTE] channel=%u %s %s %s %s (flags=0x%08x)",
		     video_channel, direction, media, transport, state, (unsigned int)flags);
		return QCAP_RT_OK;
	}

	static void stop_qcap_dante_rx(qcap_dante_rx_t* rx) {
		if (!rx) return;

		LOGI("Stopping Dante RX pipeline (QCAP API)...");
		if (rx->client) {
			if (rx->client_started) {
				QRESULT qres = QCAP_STOP_BROADCAST_CLIENT(rx->client);
				if (qres != QCAP_RS_SUCCESSFUL)
					LOGW("QCAP_STOP_BROADCAST_CLIENT failed, qres=%d", qres);
			}
			QRESULT qres = QCAP_DESTROY_BROADCAST_CLIENT(rx->client);
			if (qres != QCAP_RS_SUCCESSFUL)
				LOGW("QCAP_DESTROY_BROADCAST_CLIENT failed, qres=%d", qres);
			rx->client = nullptr;
			rx->client_started = false;
		}
		if (rx->server) {
			if (rx->server_started) {
				QRESULT qres = QCAP_STOP_DANTE_SERVER(rx->server);
				if (qres != QCAP_RS_SUCCESSFUL)
					LOGW("QCAP_STOP_DANTE_SERVER failed, qres=%d", qres);
			}
			QRESULT qres = QCAP_DESTROY_DANTE_SERVER(rx->server);
			if (qres != QCAP_RS_SUCCESSFUL)
				LOGW("QCAP_DESTROY_DANTE_SERVER failed, qres=%d", qres);
			rx->server = nullptr;
			 rx->server_started = false;
		}
		stop_qcap_media_rx(rx->media_rx);
		rx->media_rx = nullptr;
		delete rx;
		LOGI("Dante RX pipeline (QCAP API) stopped.");
	}

	static qcap_dante_rx_t* start_qcap_dante_rx(bool display_enabled) {
		qcap_dante_rx_t* rx = new qcap_dante_rx_t();
		rx->media_rx = start_qcap_media_rx(display_enabled);
		if (!rx->media_rx) {
			delete rx;
			return nullptr;
		}
		QRESULT qres = QCAP_CREATE_DANTE_SERVER(&rx->server);
		if (qres != QCAP_RS_SUCCESSFUL) {
			LOGE("QCAP_CREATE_DANTE_SERVER failed, qres=%d (requires the Dante DEP service and DanteEP shared memory)", qres);
			stop_qcap_dante_rx(rx);
			return nullptr;
		}

		qres = QCAP_SET_DANTE_SERVER_CUSTOM_PROPERTY(rx->server, 0, 1);
		if (qres != QCAP_RS_SUCCESSFUL) {
			LOGE("QCAP_SET_DANTE_SERVER_CUSTOM_PROPERTY(0, 1) failed, qres=%d", qres);
			stop_qcap_dante_rx(rx);
			return nullptr;
		}
		qres = QCAP_REGISTER_DANTE_SERVER_MESSAGE_CALLBACK(
			rx->server, qcap_dante_message_callback, rx);
		if (qres != QCAP_RS_SUCCESSFUL) {
			LOGE("QCAP_REGISTER_DANTE_SERVER_MESSAGE_CALLBACK failed, qres=%d", qres);
			stop_qcap_dante_rx(rx);
			return nullptr;
		}
		qres = QCAP_START_DANTE_SERVER(rx->server);
		if (qres != QCAP_RS_SUCCESSFUL) {
			LOGE("QCAP_START_DANTE_SERVER failed, qres=%d", qres);
			stop_qcap_dante_rx(rx);
			return nullptr;
		}
		rx->server_started = true;

		qres = QCAP_CREATE_DANTE_RECEIVER(
			rx->server, 0, 0, 0, &rx->client, QCAP_DECODER_TYPE_SOFTWARE,
			NULL, FALSE, FALSE);
		if (qres != QCAP_RS_SUCCESSFUL) {
			LOGE("QCAP_CREATE_DANTE_RECEIVER(video=0, audio=0, decoder=SOFTWARE) failed, qres=%d", qres);
			stop_qcap_dante_rx(rx);
			return nullptr;
		}
		BOOL decode = FALSE;
		qres = QCAP_SET_BROADCAST_CLIENT_CUSTOM_PROPERTY_EX(rx->client,
			QCAP_BCPROP_VIDEO_DECODE, (BYTE*)&decode, sizeof(decode));
		if (qres != QCAP_RS_SUCCESSFUL) {
			LOGE("QCAP_BCPROP_VIDEO_DECODE=0 failed, qres=%d", qres);
			stop_qcap_dante_rx(rx);
			return nullptr;
		}
		qres = QCAP_SET_BROADCAST_CLIENT_CUSTOM_PROPERTY_EX(rx->client,
			QCAP_BCPROP_AUDIO_DECODE, (BYTE*)&decode, sizeof(decode));
		if (qres != QCAP_RS_SUCCESSFUL) {
			LOGE("QCAP_BCPROP_AUDIO_DECODE=0 failed, qres=%d", qres);
			stop_qcap_dante_rx(rx);
			return nullptr;
		}
		qres = QCAP_REGISTER_BROADCAST_CLIENT_CONNECTED_CALLBACK(rx->client,
			qcap_dante_connected_callback, rx);
		if (qres != QCAP_RS_SUCCESSFUL) {
			LOGE("QCAP_REGISTER_BROADCAST_CLIENT_CONNECTED_CALLBACK failed, qres=%d", qres);
			stop_qcap_dante_rx(rx);
			return nullptr;
		}
		qres = QCAP_REGISTER_BROADCAST_CLIENT_FAIL_CALLBACK(rx->client,
			qcap_dante_fail_callback, rx);
		if (qres != QCAP_RS_SUCCESSFUL) {
			LOGE("QCAP_REGISTER_BROADCAST_CLIENT_FAIL_CALLBACK failed, qres=%d", qres);
			stop_qcap_dante_rx(rx);
			return nullptr;
		}
		qres = QCAP_REGISTER_VIDEO_BROADCAST_CLIENT_CALLBACK(rx->client,
			qcap_dante_video_callback, rx);
		if (qres != QCAP_RS_SUCCESSFUL) {
			LOGE("QCAP_REGISTER_VIDEO_BROADCAST_CLIENT_CALLBACK failed, qres=%d", qres);
			stop_qcap_dante_rx(rx);
			return nullptr;
		}
		qres = QCAP_REGISTER_AUDIO_BROADCAST_CLIENT_CALLBACK(rx->client,
			qcap_dante_audio_callback, rx);
		if (qres != QCAP_RS_SUCCESSFUL) {
			LOGE("QCAP_REGISTER_AUDIO_BROADCAST_CLIENT_CALLBACK failed, qres=%d", qres);
			stop_qcap_dante_rx(rx);
			return nullptr;
		}
		qres = QCAP_START_BROADCAST_CLIENT(rx->client, QCAP_BROADCAST_PROTOCOL_UDP, 3000, 0);
		if (qres != QCAP_RS_SUCCESSFUL) {
			LOGE("QCAP_START_BROADCAST_CLIENT failed, qres=%d", qres);
			stop_qcap_dante_rx(rx);
			return nullptr;
		}
		rx->client_started = true;

		LOGI("Dante RX pipeline started through QCAP API (video/audio decode disabled; callbacks feed QCAP2/ALSA).");
		return rx;
	}

	/* ── Master zstreamer Dante RX Pipeline Structure ────────────────── */
	struct zst_dante_rx_t {
		zst_pipeline_t* pipe = nullptr;
		zst_scheduler_t* sched = nullptr;
		zst_clock_t* clock = nullptr;
		zst_element_t* session = nullptr;
		zst_element_t* coordinator = nullptr;
		zst_element_t* rx_dep_src = nullptr;
		zst_element_t* rx_video_sink = nullptr;
		zst_element_t* h264_parser = nullptr;
		zst_element_t* h264_caps_sink = nullptr;
		zst_element_t* rx_audio_sink = nullptr;

		zst_pad_t* rx_video_pad = nullptr;

		rx_video_stats_t* video_stats = nullptr;
		rx_audio_stats_t* audio_stats = nullptr;

		// qcap2 ALLEGRO2 hardware decoder (async event-handler based)
		qcap2_video_decoder_t*       vdec = nullptr;
		qcap2_video_sink_t*          vdec_sink = nullptr;
		snd_pcm_t*                    audio_pcm = nullptr;
		int                           drm_fd = -1;
		uint32_t                      drm_connector_id = 0;
		uint32_t                      drm_crtc_id = 0;
		uint32_t                      drm_primary_plane_id = 0;
		uint32_t                      drm_video_plane_id = 0;
		uint32_t                      drm_primary_fb_id = 0;
		uint32_t                      drm_primary_bo_handle = 0;
		uint32_t                      drm_mode_blob_id = 0;
		drmModeModeInfo               drm_output_mode = {};
		bool                          drm_modeset_active = false;
		qcap2_event_t*               vdec_event = nullptr;
		qcap2_event_handlers_t*      vdec_event_handlers = nullptr;
		qcap2_packet_pool_t*         vdec_packet_pool = nullptr;
		bool                         vdec_started = false;
		bool                         vdec_sink_started = false;
		bool                         audio_pcm_started = false;
		bool                         vdec_init_done = false;
		uint32_t                      dec_width = 0;
		uint32_t                      dec_height = 0;
		double                        dec_fps = 60.0;
		uint8_t*                      extra_data = nullptr;
		int                           extra_data_size = 0;
		std::atomic<uint64_t>        decoded_frames{0};
		std::atomic<uint64_t>        decoded_errors{0};
		std::atomic<uint64_t>        decoder_display_errors{0};
		std::atomic<uint64_t>        audio_playback_errors{0};
		std::atomic<uint64_t>        audio_unsupported_periods{0};
		std::vector<uint8_t>         audio_pending_frame;
		uint64_t                      audio_pending_pts = 0;
		std::atomic<uint64_t>        decoder_input_packets{0};
		std::atomic<uint64_t>        decoder_pushed_packets{0};
		std::atomic<uint64_t>        decoder_dropped_packets{0};
		std::atomic<uint64_t>        transport_dropped_packets{0};
		std::atomic<uint64_t>        packet_pool_dropped_packets{0};
		std::atomic<uint64_t>        decoder_push_failed_packets{0};
		std::atomic<bool>            decoder_initializing{false};
		std::atomic<bool>            decoder_wait_for_idr{false};
		std::mutex                   decoder_lock;
		std::mutex                   h264_parser_lock;
		std::mutex                   display_lock;
		std::thread                  vdec_init_thread;
		std::mutex                   audio_lock;
		std::string                  reconstructed_path;
		FILE*                        reconstructed_file = nullptr;

		std::string dvr_socket{"/var/run/dante/dvr"};
		std::string dep_name{"DanteEP"};
		std::string channels{"0,1"};
		uint32_t sample_rate{48000};
		uint32_t stat_interval_sec{3};
		/* RTP reorder tolerance (coordinator must be programmed before the
		 * first flow is created; valid: window 1..4096, timeout 1..10000ms). */
		uint32_t reorder_window{1};
		uint32_t reorder_timeout_ms{1};

		/* Direct RTP verification mode (bypasses the Dante daemon/session). */
		bool direct_mode = false;
		bool decode_enabled = true;
		/* RTSP already supplies parsed AUs and caps from its pipeline parser. */
		bool h264_input_parsed = false;
		const char* media_source_name = "AVH";
		/* Decode-only isolation: run VCU decode + pop counting without
		 * DRM modeset / vsink display on plane 36. */
		std::atomic<bool> display_enabled{true};
		bool auto_mode = false;
		std::atomic<uint32_t> auto_flow_count{0};
		std::string direct_transmitter;
		std::string direct_local{"0.0.0.0"};
		uint16_t direct_port = 16490;

		pthread_t bus_th = 0;
		pthread_t sched_th = 0;
		std::atomic<bool> running{false};
		uint64_t start_time_ms{0};
	};

	struct rx_h264_caps_sink_priv_t {
		zst_dante_rx_t* rx;
	};

	static zst_result_t rx_h264_apply_caps(zst_dante_rx_t* rx, const zst_caps_t* caps) {
		if (!rx || !caps || !caps->structs ||
		    strcmp(caps->structs->media_type, "video/x-h264") != 0)
			return ZST_OK;

		const void* avcc = nullptr;
		size_t avcc_size = 0;
		uint32_t width = 0, height = 0;
		double fps = 0.0;
		if (zst_caps_get_buffer(caps, ZST_H26X_PARSER_CAPS_AVCC,
		                        &avcc, &avcc_size) != ZST_OK || !avcc ||
		    avcc_size < 11 || avcc_size > INT_MAX ||
		    zst_caps_get_uint(caps, "width", &width) != ZST_OK ||
		    zst_caps_get_uint(caps, "height", &height) != ZST_OK || !width || !height)
			return ZST_OK;
		if (zst_caps_get_double(caps, ZST_H26X_PARSER_CAPS_FRAMERATE, &fps) != ZST_OK ||
		    fps < 1.0 || fps > 240.0)
			fps = 60.0;
		uint8_t* copy = (uint8_t*)malloc(avcc_size);
		if (!copy) return ZST_ERROR;
		memcpy(copy, avcc, avcc_size);

		std::lock_guard<std::mutex> lock(rx->decoder_lock);
		if (rx->vdec_started || rx->decoder_initializing.load(std::memory_order_acquire)) {
			const bool unchanged = rx->extra_data && rx->extra_data_size == (int)avcc_size &&
			                       memcmp(rx->extra_data, avcc, avcc_size) == 0;
			free(copy);
			if (!unchanged)
				LOGW("H.264 parameter sets changed after ALLEGRO2 initialization; keeping active avcC");
			return ZST_OK;
		}

		free(rx->extra_data);
		rx->extra_data = copy;
		rx->extra_data_size = (int)avcc_size;
		rx->dec_width = width;
		rx->dec_height = height;
		rx->dec_fps = fps;
		LOGI("h26xparse supplied ALLEGRO2 format: %ux%u @ %.2f fps, avcC=%zu bytes",
		     width, height, fps, avcc_size);
		return ZST_OK;
	}

	static zst_result_t rx_h264_caps_sink_event(zst_element_t* el, zst_pad_t*,
	                                            zst_pad_event_t* event) {
		if (!el || !el->priv || !event || event->type != ZST_PAD_EVENT_CAPS)
			return ZST_OK;
		auto* priv = (rx_h264_caps_sink_priv_t*)el->priv;
		return rx_h264_apply_caps(priv->rx, event->as.caps.caps);
	}

	static zst_result_t rx_h264_caps_sink_push(zst_pad_t*, zst_buffer_t*) {
		/* This side branch only observes parser caps. Decoder input remains the
		 * original Annex-B AU on the independent media input path. */
		return ZST_OK;
	}

	static zst_element_ops_t g_rx_h264_caps_sink_ops = {
		.name = "dante_rx_h264_caps_sink",
		.event = rx_h264_caps_sink_event
	};

	static int rx_h264_parser_create(zst_dante_rx_t* rx) {
		if (!rx) return -1;
		zst_pad_t* sink_pad = nullptr;
		rx->h264_parser = zst_h26x_parser_create();
		rx_h264_caps_sink_priv_t* priv =
			(rx_h264_caps_sink_priv_t*)calloc(1, sizeof(rx_h264_caps_sink_priv_t));
		if (!rx->h264_parser || !priv) {
			free(priv);
			goto fail;
		}
		priv->rx = rx;
		rx->h264_caps_sink = zst_element_create(&g_rx_h264_caps_sink_ops, priv);
		if (!rx->h264_caps_sink) {
			free(priv);
			goto fail;
		}

		sink_pad = zst_pad_create("sink", ZST_PAD_SINK);
		if (!sink_pad) goto fail;
		sink_pad->push = rx_h264_caps_sink_push;
		if (zst_element_add_pad(rx->h264_caps_sink, sink_pad) != ZST_OK) {
			zst_pad_destroy(sink_pad);
			goto fail;
		}
		if (zst_element_set_property_string(rx->h264_parser, "codec", "h264") != ZST_OK ||
		    zst_element_set_property_string(rx->h264_parser, "input-stream-format", "byte-stream") != ZST_OK ||
		    zst_pad_link(zst_element_get_pad(rx->h264_parser, "src"),
		                 zst_element_get_pad(rx->h264_caps_sink, "sink")) != ZST_OK ||
		    zst_element_set_state(rx->h264_parser, ZST_STATE_READY) != ZST_OK ||
		    zst_element_set_state(rx->h264_caps_sink, ZST_STATE_READY) != ZST_OK ||
		    zst_element_set_state(rx->h264_parser, ZST_STATE_PLAYING) != ZST_OK ||
		    zst_element_set_state(rx->h264_caps_sink, ZST_STATE_PLAYING) != ZST_OK) {
			goto fail;
		}
		return 0;

	fail:
		LOGE("Failed to create/configure h26xparse avcC observer");
		if (rx->h264_parser) zst_element_destroy(rx->h264_parser);
		if (rx->h264_caps_sink) zst_element_destroy(rx->h264_caps_sink);
		rx->h264_parser = nullptr;
		rx->h264_caps_sink = nullptr;
		return -1;
	}

	static void rx_h264_parser_destroy(zst_dante_rx_t* rx) {
		if (!rx) return;
		if (rx->h264_parser) zst_element_set_state(rx->h264_parser, ZST_STATE_NULL);
		if (rx->h264_caps_sink) zst_element_set_state(rx->h264_caps_sink, ZST_STATE_NULL);
		if (rx->h264_parser) zst_element_destroy(rx->h264_parser);
		if (rx->h264_caps_sink) zst_element_destroy(rx->h264_caps_sink);
		rx->h264_parser = nullptr;
		rx->h264_caps_sink = nullptr;
	}

	static zst_result_t rx_h264_parser_push(zst_dante_rx_t* rx,
	                                       const uint8_t* data, size_t size, uint64_t pts) {
		if (!rx || !rx->h264_parser || !data || !size) return ZST_ERROR;
		zst_pad_t* sink = zst_element_get_pad(rx->h264_parser, "sink");
		if (!sink || !sink->push) return ZST_ERROR;
		zst_buffer_t* input = zst_buffer_create(ZST_BUFFER_VIDEO_PACKET);
		if (!input) return ZST_ERROR;
		uint8_t* copy = (uint8_t*)malloc(size);
		if (!copy) { zst_buffer_unref(input); return ZST_ERROR; }
		memcpy(copy, data, size);
		input->memory.type = ZST_MEMORY_CPU;
		input->memory.data = copy;
		input->memory.size = size;
		input->memory.priv = copy;
		input->memory.release = free;
		input->pts = input->dts = pts;
		std::lock_guard<std::mutex> lock(rx->h264_parser_lock);
		zst_result_t result = sink->push(sink, input);
		zst_buffer_unref(input);
		return result;
	}

	static int select_hdmi_audio_route(unsigned int route) {
		snd_ctl_t* control = nullptr;
		int err = snd_ctl_open(&control, "hw:xlnxhdmi1", 0);
		if (err < 0) {
			LOGE("snd_ctl_open(xlnxhdmi1) failed: %s", snd_strerror(err));
			return -1;
		}
		snd_ctl_elem_id_t* id;
		snd_ctl_elem_value_t* value;
		snd_ctl_elem_id_alloca(&id);
		snd_ctl_elem_value_alloca(&value);
		snd_ctl_elem_id_set_interface(id, SND_CTL_ELEM_IFACE_MIXER);
		snd_ctl_elem_id_set_name(id, "Audio Input Route Switch");
		snd_ctl_elem_value_set_id(value, id);
		snd_ctl_elem_value_set_enumerated(value, 0, route);
		err = snd_ctl_elem_write(control, value);
		snd_ctl_close(control);
		if (err < 0) {
			LOGE("cannot select HDMI audio route %u: %s", route, snd_strerror(err));
			return -1;
		}
		return 0;
	}

	static int read_hdmi_audio_route() {
		snd_ctl_t* control = nullptr;
		int err = snd_ctl_open(&control, "hw:xlnxhdmi1", 0);
		if (err < 0) {
			LOGE("snd_ctl_open(xlnxhdmi1) failed: %s", snd_strerror(err));
			return -1;
		}
		snd_ctl_elem_id_t* id;
		snd_ctl_elem_value_t* value;
		snd_ctl_elem_id_alloca(&id);
		snd_ctl_elem_value_alloca(&value);
		snd_ctl_elem_id_set_interface(id, SND_CTL_ELEM_IFACE_MIXER);
		snd_ctl_elem_id_set_name(id, "Audio Input Route Switch");
		snd_ctl_elem_value_set_id(value, id);
		err = snd_ctl_elem_read(control, value);
		snd_ctl_close(control);
		if (err < 0) {
			LOGE("cannot read HDMI audio route: %s", snd_strerror(err));
			return -1;
		}
		return snd_ctl_elem_value_get_enumerated(value, 0);
	}

	static const char* hdmi_audio_route_name(int route) {
		switch (route) {
		case 0:
			return "PS DDR (Audio Formatter)";
		case 1:
			return "Custom FPGA (Dante RX)";
		default:
			return "unknown";
		}
	}

	static int start_hdmi_audio_sink(zst_dante_rx_t* rx) {
		if (select_hdmi_audio_route(0) != 0)
			return -1;
		rx->audio_pending_frame.reserve(960 * 2 * sizeof(int32_t));
		int err = snd_pcm_open(&rx->audio_pcm, "hw:xlnxhdmi1,0", SND_PCM_STREAM_PLAYBACK,
			SND_PCM_NONBLOCK);
		if (err >= 0)
			err = snd_pcm_set_params(rx->audio_pcm, SND_PCM_FORMAT_S24_LE,
				SND_PCM_ACCESS_RW_INTERLEAVED, 2, 48000, 0, 160000);
		if (err < 0) {
			LOGE("cannot configure hw:xlnxhdmi1,0: %s", snd_strerror(err));
			if (rx->audio_pcm) snd_pcm_close(rx->audio_pcm);
			rx->audio_pcm = nullptr;
			return -1;
		}
		rx->audio_pcm_started = true;
		LOGI("HDMI audio output active: xlnxhdmi1,0 via audio formatter");
		return 0;
	}

	static void stop_hdmi_audio_sink(zst_dante_rx_t* rx) {
		if (rx->audio_pcm) snd_pcm_close(rx->audio_pcm);
		rx->audio_pcm = nullptr;
		rx->audio_pcm_started = false;
	}

	static void rx_audio_playback(zst_dante_rx_t* rx, const zst_buffer_t* buf,
		const zst_audio_frame_t* audio) {
		if (!rx || !rx->audio_pcm || !rx->audio_pcm_started ||
			!buf || !buf->memory.data)
			return;
		const uint32_t channels = audio ? audio->channels : 2;
		const uint32_t rate = audio ? audio->sample_rate : 48000;
		const uint32_t sample_format = audio ? audio->format : ZST_AUDIO_FMT_S32LE;
		const size_t samples = audio ? audio->nb_samples : buf->memory.size / 8;
		if (channels != 2 || rate != 48000 || sample_format != ZST_AUDIO_FMT_S32LE ||
			samples == 0 || buf->memory.size != samples * 8) {
			rx->audio_unsupported_periods++;
			return;
		}

		std::lock_guard<std::mutex> lock(rx->audio_lock);
		const size_t input_bytes = samples * channels * sizeof(int32_t);
		const size_t output_bytes = 960 * 2 * sizeof(int32_t);
		if (rx->audio_pending_frame.empty())
			rx->audio_pending_pts = buf->pts;
		const size_t offset = rx->audio_pending_frame.size();
		rx->audio_pending_frame.resize(offset + input_bytes);
		const uint8_t* src = (const uint8_t*)buf->memory.data;
		for (size_t i = 0; i < samples * channels; ++i) {
			int32_t sample;
			memcpy(&sample, src + i * sizeof(sample), sizeof(sample));
			sample >>= 8;
			uint8_t* dst = &rx->audio_pending_frame[offset + i * 4];
			// ALSA S24_LE uses a 32-bit slot with its 24 valid bits right-aligned.
			dst[0] = (uint8_t)sample;
			dst[1] = (uint8_t)(sample >> 8);
			dst[2] = (uint8_t)(sample >> 16);
			dst[3] = 0;
		}
		/* AAC commonly produces 1024 samples; preserve the remainder while
		 * feeding the same 960-sample HDMI periods used by AVH PCM. */
		while (rx->audio_pending_frame.size() >= output_bytes) {
			snd_pcm_sframes_t written = snd_pcm_writei(rx->audio_pcm,
				rx->audio_pending_frame.data(), 960);
			rx->audio_pending_frame.erase(rx->audio_pending_frame.begin(),
				rx->audio_pending_frame.begin() + output_bytes);
			if (written != 960 && written != -EAGAIN) {
				if (written < 0 && (written == -EPIPE || written == -ESTRPIPE))
					snd_pcm_prepare(rx->audio_pcm);
				rx->audio_playback_errors++;
			}
		}
	}

	/* ── qcap2 ALLEGRO2 hardware decoder ────────────────────────────── */

	static QRETURN on_vdec_event_callback(PVOID pUserData);
	static int start_drm_output(zst_dante_rx_t* rx);
	static int start_vdec_sink(zst_dante_rx_t* rx);
	static inline float mode_vrefresh(const drmModeModeInfo* mode);


	static void StopVdecAsync(zst_dante_rx_t* rx) {
		if (rx->vdec_event_handlers) {
			qcap2_event_handlers_stop(rx->vdec_event_handlers);
			qcap2_event_handlers_delete(rx->vdec_event_handlers);
			rx->vdec_event_handlers = nullptr;
		}
		if (rx->vdec_packet_pool) {
			qcap2_packet_pool_stop(rx->vdec_packet_pool);
			qcap2_packet_pool_delete(rx->vdec_packet_pool);
			rx->vdec_packet_pool = nullptr;
		}
		if (rx->vdec_event) {
			qcap2_event_stop(rx->vdec_event);
			qcap2_event_delete(rx->vdec_event);
			rx->vdec_event = nullptr;
		}
		rx->vdec_init_done = false;
	}

	/* Create decoder event, output handler, and packet pool before decoder start. */
	static int SetupVdecAsync(zst_dante_rx_t* rx) {
		QRESULT qres;
		uintptr_t event_handle;

		rx->vdec_event = qcap2_event_new();
		if (!rx->vdec_event) {
			LOGE("qcap2_event_new() failed");
			return -1;
		}
		qres = qcap2_event_start(rx->vdec_event);
		if (qres != QCAP_RS_SUCCESSFUL) {
			LOGE("qcap2_event_start() failed, qres=%d", (int)qres);
			StopVdecAsync(rx);
			return -1;
		}

		rx->vdec_event_handlers = qcap2_event_handlers_new();
		if (!rx->vdec_event_handlers) {
			LOGE("qcap2_event_handlers_new() failed");
			StopVdecAsync(rx);
			return -1;
		}
		qres = qcap2_event_handlers_start(rx->vdec_event_handlers);
		if (qres != QCAP_RS_SUCCESSFUL) {
			LOGE("qcap2_event_handlers_start() failed, qres=%d", (int)qres);
			StopVdecAsync(rx);
			return -1;
		}
		qres = qcap2_event_get_native_handle(rx->vdec_event, &event_handle);
		if (qres != QCAP_RS_SUCCESSFUL) {
			LOGE("qcap2_event_get_native_handle() failed, qres=%d", (int)qres);
			StopVdecAsync(rx);
			return -1;
		}
		qres = qcap2_event_handlers_add_handler(rx->vdec_event_handlers, event_handle,
			on_vdec_event_callback, rx);
		if (qres != QCAP_RS_SUCCESSFUL) {
			LOGE("qcap2_event_handlers_add_handler(vdec) failed, qres=%d", (int)qres);
			StopVdecAsync(rx);
			return -1;
		}

		rx->vdec_packet_pool = qcap2_packet_pool_new();
		if (!rx->vdec_packet_pool) {
			LOGE("qcap2_packet_pool_new() failed");
			StopVdecAsync(rx);
			return -1;
		}
		qcap2_packet_pool_set_packet_count(rx->vdec_packet_pool, 16);
		qres = qcap2_packet_pool_start(rx->vdec_packet_pool);
		if (qres != QCAP_RS_SUCCESSFUL) {
			LOGE("qcap2_packet_pool_start() failed, qres=%d", (int)qres);
			StopVdecAsync(rx);
			return -1;
		}

		rx->vdec_init_done = true;
		return 0;
	}

	/* Decoder output is fully event-driven; no timer polling is used. */
	static QRETURN on_vdec_event_callback(PVOID pUserData) {
		zst_dante_rx_t* rx = (zst_dante_rx_t*)pUserData;
		if (!rx) return QCAP_RT_OK;

		while (true) {
			qcap2_rcbuffer_t* pFrame = nullptr;
			QRESULT qres;
			{
				std::lock_guard<std::mutex> decoder_guard(rx->decoder_lock);
				if (!rx->vdec || !rx->vdec_started) return QCAP_RT_OK;
				qres = qcap2_video_decoder_pop(rx->vdec, &pFrame);
			}
			if (qres != QCAP_RS_SUCCESSFUL) {
				if (qres != QCAP_RS_ERROR_TIMEOUT)
					rx->decoded_errors++;
				break;
			}
			rx->decoded_frames++;
			{
				std::lock_guard<std::mutex> display_guard(rx->display_lock);
				if (rx->display_enabled && rx->vdec_sink && rx->vdec_sink_started) {
					qres = qcap2_video_sink_push(rx->vdec_sink, pFrame);
					if (qres != QCAP_RS_SUCCESSFUL) {
						rx->decoder_display_errors++;
						LOGE("qcap2_video_sink_push() failed, qres=%d", (int)qres);
					}
				}
			}
			qcap2_rcbuffer_release(pFrame);
		}
		return QCAP_RT_OK;
	}

	static void submit_vdec_packet(zst_dante_rx_t* rx, const uint8_t* data,
		size_t data_size, bool is_keyframe, uint64_t pts) {
		qcap2_rcbuffer_t* rcbuf = nullptr;
		QRESULT qres = qcap2_packet_pool_get_buffer(rx->vdec_packet_pool,
			(int)data_size, &rcbuf);
		if (qres != QCAP_RS_SUCCESSFUL || !rcbuf) {
			rx->decoder_dropped_packets++;
			rx->packet_pool_dropped_packets++;
			if (qres != QCAP_RS_ERROR_TIMEOUT)
				LOGE("qcap2_packet_pool_get_buffer() failed, qres=%d", (int)qres);
			return;
		}

		qcap2_av_packet_t* pkt = (qcap2_av_packet_t*)qcap2_rcbuffer_lock_data(rcbuf);
		uint8_t* dst = nullptr;
		int dst_size = 0;
		if (pkt)
			qcap2_av_packet_get_buffer(pkt, &dst, &dst_size);
		if (!pkt || !dst || dst_size < (int)data_size) {
			if (pkt) qcap2_rcbuffer_unlock_data(rcbuf);
			qcap2_rcbuffer_release(rcbuf);
			rx->decoder_dropped_packets++;
			rx->decoded_errors++;
			LOGE("packet pool returned an invalid %zu-byte input buffer", data_size);
			return;
		}

		memcpy(dst, data, data_size);
		qcap2_av_packet_set_buffer(pkt, dst, (int)data_size);
		qcap2_av_packet_set_property(pkt, 0, is_keyframe ? TRUE : FALSE);
		qcap2_av_packet_set_pts(pkt, (int64_t)(pts / 1000));
		qcap2_av_packet_set_dts(pkt, (int64_t)(pts / 1000));
		qcap2_rcbuffer_unlock_data(rcbuf);

		qres = qcap2_video_decoder_push(rx->vdec, rcbuf);
		qcap2_rcbuffer_release(rcbuf);
		if (qres == QCAP_RS_SUCCESSFUL) {
			rx->decoder_pushed_packets++;
		} else {
			rx->decoder_dropped_packets++;
			rx->decoder_push_failed_packets++;
			if (qres != QCAP_RS_ERROR_TIMEOUT)
				LOGE("qcap2_video_decoder_push() failed, qres=%d", (int)qres);
		}
	}

	static int init_vdec_decoder(zst_dante_rx_t* rx) {
		/* 1. Create decoder */
		rx->vdec = qcap2_video_decoder_new();
		if (!rx->vdec) {
			LOGE("qcap2_video_decoder_new() failed");
			return -1;
		}

		std::shared_ptr<qcap2_video_encoder_property_t> prop(
			qcap2_video_encoder_property_new(), qcap2_video_encoder_property_delete);
		qcap2_video_encoder_property_set_property(prop.get(),
			QCAP_ENCODER_TYPE_ALLEGRO2, QCAP_ENCODER_FORMAT_H264,
			QCAP_COLORSPACE_TYPE_NV12,
			rx->dec_width, rx->dec_height, rx->dec_fps,
			0, 0, 0, 0, 0, 0);
		qcap2_video_decoder_set_video_property(rx->vdec, prop.get());
		qcap2_video_decoder_set_extra_data(rx->vdec, rx->extra_data, rx->extra_data_size);

		/* Media adapters provide one complete Annex-B AU per buffer. */
		qcap2_video_decoder_set_input_mode(rx->vdec, AL_DEC_SPLIT_INPUT);
		qcap2_video_decoder_set_multithread(rx->vdec, false);
		qcap2_video_decoder_set_frame_count(rx->vdec, 8);
		qcap2_video_decoder_set_packet_count(rx->vdec, 16);
		qcap2_video_decoder_set_max_packet_size(rx->vdec, 1024 * 1024);

		/* 4.5 Bind decoder event for async output */
		qcap2_video_decoder_set_event(rx->vdec, rx->vdec_event);

		/* 5. Start decoder */
		QRESULT qres = qcap2_video_decoder_start(rx->vdec);
		if (qres != QCAP_RS_SUCCESSFUL) {
			LOGE("qcap2_video_decoder_start() failed, qres=%d", (int)qres);
			qcap2_video_decoder_delete(rx->vdec);
			rx->vdec = nullptr;
			return -1;
		}

		rx->vdec_started = true;
		LOGI("qcap2 ALLEGRO2 decoder configured: %ux%u @ %.2f fps",
			rx->dec_width, rx->dec_height, rx->dec_fps);
		return 0;
	}

	/* DRM modeset can take tens of seconds. It must not run in the RTP
	 * coordinator callback, which otherwise holds the reorder path and later
	 * bursts stale AUs into the decoder. */
	static void start_vdec_decoder_async(zst_dante_rx_t* rx) {
		if (rx->vdec_init_thread.joinable())
			rx->vdec_init_thread.join();
		rx->decoder_initializing.store(true, std::memory_order_release);
		rx->decoder_wait_for_idr.store(true, std::memory_order_release);
		rx->vdec_init_thread = std::thread([rx]() {
			bool ready = true;
			/* Select HDMI timing from EDID using the source format, then configure
			 * the shared VProcSS destination before decoder output begins flips. */
			if (rx->display_enabled &&
				(start_drm_output(rx) != 0 || start_vdec_sink(rx) != 0))
				ready = false;
			if (ready) {
				std::lock_guard<std::mutex> decoder_guard(rx->decoder_lock);
				ready = SetupVdecAsync(rx) == 0 && init_vdec_decoder(rx) == 0;
				if (!ready) StopVdecAsync(rx);
			}
			if (!ready) {
				LOGE("ALLEGRO2 decoder initialization failed");
				rx->decoder_wait_for_idr.store(false, std::memory_order_release);
			}
			rx->decoder_initializing.store(false, std::memory_order_release);
		});
	}

	static void stop_vdec_decoder(zst_dante_rx_t* rx) {
		if (rx->vdec_init_thread.joinable())
			rx->vdec_init_thread.join();
		/* Stop joins output callbacks before taking decoder_lock. */
		if (rx->vdec_event_handlers) {
			qcap2_event_handlers_stop(rx->vdec_event_handlers);
			qcap2_event_handlers_delete(rx->vdec_event_handlers);
			rx->vdec_event_handlers = nullptr;
		}
		std::lock_guard<std::mutex> decoder_guard(rx->decoder_lock);
		if (rx->vdec) {
			if (rx->vdec_started) {
				qcap2_video_decoder_stop(rx->vdec);
			}
			qcap2_video_decoder_delete(rx->vdec);
			rx->vdec = nullptr;
			rx->vdec_started = false;
		}
		StopVdecAsync(rx);
	}

	static void stop_drm_output(zst_dante_rx_t* rx) {
		if (rx->drm_fd < 0) return;
		if (rx->drm_modeset_active) {
			std::shared_ptr<drmModeAtomicReq> request(drmModeAtomicAlloc(), drmModeAtomicFree);
			if (request) {
				uint32_t plane_crtc = drm_property_id(rx->drm_fd, rx->drm_primary_plane_id,
					DRM_MODE_OBJECT_PLANE, "CRTC_ID");
				uint32_t plane_fb = drm_property_id(rx->drm_fd, rx->drm_primary_plane_id,
					DRM_MODE_OBJECT_PLANE, "FB_ID");
				uint32_t crtc_active = drm_property_id(rx->drm_fd, rx->drm_crtc_id,
					DRM_MODE_OBJECT_CRTC, "ACTIVE");
				uint32_t connector_crtc = drm_property_id(rx->drm_fd, rx->drm_connector_id,
					DRM_MODE_OBJECT_CONNECTOR, "CRTC_ID");
				if (plane_crtc) drmModeAtomicAddProperty(request.get(), rx->drm_primary_plane_id, plane_crtc, 0);
				if (plane_fb) drmModeAtomicAddProperty(request.get(), rx->drm_primary_plane_id, plane_fb, 0);
				if (crtc_active) drmModeAtomicAddProperty(request.get(), rx->drm_crtc_id, crtc_active, 0);
				if (connector_crtc) drmModeAtomicAddProperty(request.get(), rx->drm_connector_id, connector_crtc, 0);
				drmModeAtomicCommit(rx->drm_fd, request.get(), DRM_MODE_ATOMIC_ALLOW_MODESET, nullptr);
			}
		}
		if (rx->drm_primary_fb_id)
			drmModeRmFB(rx->drm_fd, rx->drm_primary_fb_id);
		if (rx->drm_primary_bo_handle) {
			struct drm_mode_destroy_dumb destroy = {};
			destroy.handle = rx->drm_primary_bo_handle;
			drmIoctl(rx->drm_fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
		}
		if (rx->drm_mode_blob_id)
			drmModeDestroyPropertyBlob(rx->drm_fd, rx->drm_mode_blob_id);
		drmClose(rx->drm_fd);
		rx->drm_fd = -1;
		rx->drm_connector_id = 0;
		rx->drm_crtc_id = 0;
		rx->drm_primary_plane_id = 0;
		rx->drm_video_plane_id = 0;
		rx->drm_primary_fb_id = 0;
		rx->drm_primary_bo_handle = 0;
		rx->drm_mode_blob_id = 0;
		rx->drm_output_mode = {};
		rx->drm_modeset_active = false;
	}

	static int start_drm_output(zst_dante_rx_t* rx) {
		if (rx->drm_modeset_active)
			return 0;
		const uint32_t width = rx->dec_width;
		const uint32_t height = rx->dec_height;
		const uint32_t fps = (uint32_t)(rx->dec_fps + 0.5);
		rx->drm_fd = drmOpen("xlnx", nullptr);
		if (rx->drm_fd < 0 || drmSetClientCap(rx->drm_fd, DRM_CLIENT_CAP_ATOMIC, 1) ||
			drmSetClientCap(rx->drm_fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1)) {
			LOGE("cannot enable DRM atomic universal planes, err=%d", errno);
			stop_drm_output(rx);
			return -1;
		}

		std::shared_ptr<drmModeRes> resources(drmModeGetResources(rx->drm_fd), drmModeFreeResources);
		std::shared_ptr<drmModePlaneRes> planes(
			drmModeGetPlaneResources(rx->drm_fd), drmModeFreePlaneResources);
		if (!resources || !planes) {
			LOGE("DRM resources or planes are unavailable");
			stop_drm_output(rx);
			return -1;
		}
		std::shared_ptr<drmModePlane> video_plane;
		for (uint32_t i = 0; i < planes->count_planes; ++i) {
			std::shared_ptr<drmModePlane> plane(drmModeGetPlane(rx->drm_fd, planes->planes[i]), drmModeFreePlane);
			if (!plane || !drm_property_id(rx->drm_fd, plane->plane_id,
				DRM_MODE_OBJECT_PLANE, "SC6F0_OVERLAY_SOURCE")) continue;
			for (uint32_t f = 0; f < plane->count_formats; ++f) {
				if (plane->formats[f] == DRM_FORMAT_NV12) {
					video_plane = plane;
					rx->drm_video_plane_id = plane->plane_id;
					break;
				}
			}
			if (video_plane) break;
		}
		if (!video_plane) {
			LOGE("no NV12 overlay plane with SC6F0_OVERLAY_SOURCE is available");
			stop_drm_output(rx);
			return -1;
		}
		const uint32_t video_plane_id = rx->drm_video_plane_id;

		drmModeModeInfo mode = {};
		bool have_mode = false;
		bool selected_exact = false;
		int crtc_index = -1;
		for (int i = 0; i < resources->count_connectors && crtc_index < 0; ++i) {
			std::shared_ptr<drmModeConnector> connector(
				drmModeGetConnector(rx->drm_fd, resources->connectors[i]), drmModeFreeConnector);
			if (!connector || connector->connector_type != DRM_MODE_CONNECTOR_HDMIA ||
			    connector->connection != DRM_MODE_CONNECTED) continue;
			drmModeModeInfo connector_mode = {};
			bool connector_has_mode = false;
			bool exact_mode = false;
			for (int m = 0; m < connector->count_modes; ++m) {
				const drmModeModeInfo& candidate = connector->modes[m];
				/* The shared VProcSS is built for at most 3840x2160 and the
				 * HDMI TX path is qualified up to 594 MHz / 60 Hz. */
				if (!candidate.hdisplay || !candidate.vdisplay ||
				    candidate.hdisplay > 3840 || candidate.vdisplay > 2160 ||
				    candidate.clock > 594000 || mode_vrefresh(&candidate) <= 0 ||
				    mode_vrefresh(&candidate) > 60.5 ||
				    (candidate.flags & DRM_MODE_FLAG_INTERLACE)) continue;
				bool candidate_exact = candidate.hdisplay == width &&
					candidate.vdisplay == height &&
					fabs(mode_vrefresh(&candidate) - rx->dec_fps) < 0.5;
				uint64_t area = (uint64_t)candidate.hdisplay * candidate.vdisplay;
				uint64_t selected_area = (uint64_t)connector_mode.hdisplay * connector_mode.vdisplay;
				if (!connector_has_mode || (candidate_exact && !exact_mode) ||
				    (candidate_exact == exact_mode &&
				     (area > selected_area ||
				      (area == selected_area &&
				       (mode_vrefresh(&candidate) > mode_vrefresh(&connector_mode) + 0.005 ||
				        (width == 3840 && height == 2160 && fps == 60 &&
				         candidate.clock == 594000 && connector_mode.clock != 594000)))))) {
					connector_mode = candidate;
					connector_has_mode = true;
					exact_mode = candidate_exact;
				}
			}
			if (!connector_has_mode) continue;
			uint32_t encoder_id = connector->encoder_id;
			if (!encoder_id && connector->count_encoders > 0)
				encoder_id = connector->encoders[0];
			std::shared_ptr<drmModeEncoder> encoder(
				drmModeGetEncoder(rx->drm_fd, encoder_id), drmModeFreeEncoder);
			uint32_t possible_crtcs = encoder ? encoder->possible_crtcs : 0;
			for (int c = 0; c < resources->count_crtcs; ++c) {
				if ((possible_crtcs & (1u << c)) && (video_plane->possible_crtcs & (1u << c))) {
					rx->drm_connector_id = connector->connector_id;
					rx->drm_crtc_id = resources->crtcs[c];
					mode = connector_mode;
					have_mode = true;
					selected_exact = exact_mode;
					crtc_index = c;
					break;
				}
			}
		}
		if (crtc_index < 0 || !have_mode) {
			LOGE("no connected HDMI-A path with a VProcSS-compatible EDID mode supports DRM plane %u",
			     video_plane_id);
			stop_drm_output(rx);
			return -1;
		}

		for (uint32_t i = 0; i < planes->count_planes; ++i) {
			std::shared_ptr<drmModePlane> plane(drmModeGetPlane(rx->drm_fd, planes->planes[i]), drmModeFreePlane);
			if (!plane || !(plane->possible_crtcs & (1u << crtc_index))) continue;
			uint32_t type = drm_property_id(rx->drm_fd, plane->plane_id, DRM_MODE_OBJECT_PLANE, "type");
			std::shared_ptr<drmModeObjectProperties> properties(
				drmModeObjectGetProperties(rx->drm_fd, plane->plane_id, DRM_MODE_OBJECT_PLANE), drmModeFreeObjectProperties);
			if (!type || !properties) continue;
			for (uint32_t p = 0; p < properties->count_props; ++p) {
				if (properties->props[p] == type && properties->prop_values[p] == DRM_PLANE_TYPE_PRIMARY) {
					rx->drm_primary_plane_id = plane->plane_id;
					break;
				}
			}
			if (rx->drm_primary_plane_id) break;
		}
		if (!rx->drm_primary_plane_id) {
			LOGE("no primary DRM plane supports selected CRTC");
			stop_drm_output(rx);
			return -1;
		}

		struct drm_mode_create_dumb create = {};
		create.width = mode.hdisplay;
		create.height = mode.vdisplay;
		create.bpp = 24;
		if (drmIoctl(rx->drm_fd, DRM_IOCTL_MODE_CREATE_DUMB, &create) ||
			drmModeCreatePropertyBlob(rx->drm_fd, &mode, sizeof(mode), &rx->drm_mode_blob_id)) {
			LOGE("cannot create DRM primary framebuffer, err=%d", errno);
			stop_drm_output(rx);
			return -1;
		}
		rx->drm_primary_bo_handle = create.handle;
		uint32_t handles[4] = { create.handle, 0, 0, 0 };
		uint32_t pitches[4] = { create.pitch, 0, 0, 0 };
		uint32_t offsets[4] = { 0, 0, 0, 0 };
		if (drmModeAddFB2(rx->drm_fd, mode.hdisplay, mode.vdisplay, DRM_FORMAT_BGR888, handles, pitches, offsets,
			&rx->drm_primary_fb_id, 0)) {
			LOGE("drmModeAddFB2(primary) failed, err=%d", errno);
			stop_drm_output(rx);
			return -1;
		}

		std::shared_ptr<drmModeAtomicReq> request(drmModeAtomicAlloc(), drmModeAtomicFree);
		uint32_t connector_crtc = drm_property_id(rx->drm_fd, rx->drm_connector_id, DRM_MODE_OBJECT_CONNECTOR, "CRTC_ID");
		uint32_t crtc_mode = drm_property_id(rx->drm_fd, rx->drm_crtc_id, DRM_MODE_OBJECT_CRTC, "MODE_ID");
		uint32_t crtc_active = drm_property_id(rx->drm_fd, rx->drm_crtc_id, DRM_MODE_OBJECT_CRTC, "ACTIVE");
		uint32_t plane_crtc = drm_property_id(rx->drm_fd, rx->drm_primary_plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_ID");
		uint32_t plane_fb = drm_property_id(rx->drm_fd, rx->drm_primary_plane_id, DRM_MODE_OBJECT_PLANE, "FB_ID");
		uint32_t crtc_x = drm_property_id(rx->drm_fd, rx->drm_primary_plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_X");
		uint32_t crtc_y = drm_property_id(rx->drm_fd, rx->drm_primary_plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_Y");
		uint32_t crtc_w = drm_property_id(rx->drm_fd, rx->drm_primary_plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_W");
		uint32_t crtc_h = drm_property_id(rx->drm_fd, rx->drm_primary_plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_H");
		uint32_t src_x = drm_property_id(rx->drm_fd, rx->drm_primary_plane_id, DRM_MODE_OBJECT_PLANE, "SRC_X");
		uint32_t src_y = drm_property_id(rx->drm_fd, rx->drm_primary_plane_id, DRM_MODE_OBJECT_PLANE, "SRC_Y");
		uint32_t src_w = drm_property_id(rx->drm_fd, rx->drm_primary_plane_id, DRM_MODE_OBJECT_PLANE, "SRC_W");
		uint32_t src_h = drm_property_id(rx->drm_fd, rx->drm_primary_plane_id, DRM_MODE_OBJECT_PLANE, "SRC_H");
		if (!request || !connector_crtc || !crtc_mode || !crtc_active || !plane_crtc || !plane_fb ||
			!crtc_x || !crtc_y || !crtc_w || !crtc_h || !src_x || !src_y || !src_w || !src_h) {
			LOGE("required DRM atomic properties are unavailable");
			stop_drm_output(rx);
			return -1;
		}
		drmModeAtomicAddProperty(request.get(), rx->drm_connector_id, connector_crtc, rx->drm_crtc_id);
		drmModeAtomicAddProperty(request.get(), rx->drm_crtc_id, crtc_mode, rx->drm_mode_blob_id);
		drmModeAtomicAddProperty(request.get(), rx->drm_crtc_id, crtc_active, 1);
		drmModeAtomicAddProperty(request.get(), rx->drm_primary_plane_id, plane_crtc, rx->drm_crtc_id);
		drmModeAtomicAddProperty(request.get(), rx->drm_primary_plane_id, plane_fb, rx->drm_primary_fb_id);
		drmModeAtomicAddProperty(request.get(), rx->drm_primary_plane_id, crtc_x, 0);
		drmModeAtomicAddProperty(request.get(), rx->drm_primary_plane_id, crtc_y, 0);
		drmModeAtomicAddProperty(request.get(), rx->drm_primary_plane_id, crtc_w, mode.hdisplay);
		drmModeAtomicAddProperty(request.get(), rx->drm_primary_plane_id, crtc_h, mode.vdisplay);
		drmModeAtomicAddProperty(request.get(), rx->drm_primary_plane_id, src_x, 0);
		drmModeAtomicAddProperty(request.get(), rx->drm_primary_plane_id, src_y, 0);
		drmModeAtomicAddProperty(request.get(), rx->drm_primary_plane_id, src_w, (uint32_t)mode.hdisplay << 16);
		drmModeAtomicAddProperty(request.get(), rx->drm_primary_plane_id, src_h, (uint32_t)mode.vdisplay << 16);
		uint32_t overlay_source = drm_property_id(rx->drm_fd, video_plane_id,
			DRM_MODE_OBJECT_PLANE, "SC6F0_OVERLAY_SOURCE");
		/* This path displays decoded DDR frames; DUMMY is the driver default. */
		drmModeAtomicAddProperty(request.get(), video_plane_id, overlay_source, 2);
		if (drmModeAtomicCommit(rx->drm_fd, request.get(), DRM_MODE_ATOMIC_ALLOW_MODESET, nullptr)) {
			LOGE("DRM HDMI modeset failed, err=%d", errno);
			stop_drm_output(rx);
			return -1;
		}
		rx->drm_modeset_active = true;
		rx->drm_output_mode = mode;
		if (!selected_exact)
			LOGW("[%s] input %ux%u@%.2f not in EDID; scaling to %s (%ux%u@%.2f)",
			     rx->media_source_name, width, height, rx->dec_fps,
			     mode.name, mode.hdisplay, mode.vdisplay, mode_vrefresh(&mode));
		if (!selected_exact && mode_vrefresh(&mode) + 0.5 < rx->dec_fps)
			LOGW("EDID output %.2f Hz is slower than %s input %.2f fps; display frames may be dropped",
			     mode_vrefresh(&mode), rx->media_source_name, rx->dec_fps);
		LOGI("DRM HDMI active: %s %ux%u@%.2f, clock=%u kHz; %s input %ux%u@%.2f (%s); connector %u, CRTC %u, primary plane %u, video plane %u",
		     mode.name, mode.hdisplay, mode.vdisplay, mode_vrefresh(&mode), mode.clock,
		     rx->media_source_name, width, height, rx->dec_fps, selected_exact ? "exact" : "VProcSS scaling",
		     rx->drm_connector_id, rx->drm_crtc_id, rx->drm_primary_plane_id, video_plane_id);
		return 0;
	}

	static int start_vdec_sink(zst_dante_rx_t* rx) {
		if (rx->vdec_sink_started)
			return 0;
		const uint32_t drm_plane_id = rx->drm_video_plane_id;
		qcap2_video_sink_t* sink = qcap2_video_sink_new();
		if (!sink) {
			LOGE("qcap2_video_sink_new() failed");
			return -1;
		}

		qcap2_video_sink_set_backend_type(sink, QCAP2_VIDEO_SINK_BACKEND_TYPE_XLNX2);
		qcap2_video_sink_set_native_handle(sink,
			QCAP_HWND_DRM_PLANE_ID_MASK | drm_plane_id);
		/* The sink's format selects the HDMI mode; its pushed framebuffer
		 * supplies SRC_W/H. A separate DRM rect supplies CRTC_W/H to VProcSS. */
		const uint32_t out_w = rx->drm_output_mode.hdisplay;
		const uint32_t out_h = rx->drm_output_mode.vdisplay;
		if (!rx->dec_width || !rx->dec_height || !out_w || !out_h) {
			LOGE("cannot configure VProcSS without source and HDMI output dimensions");
			qcap2_video_sink_delete(sink);
			return -1;
		}
		uint32_t rect_w = out_w, rect_h = out_h;
		if ((uint64_t)rx->dec_width * out_h > (uint64_t)rx->dec_height * out_w)
			rect_h = (uint64_t)out_w * rx->dec_height / rx->dec_width;
		else
			rect_w = (uint64_t)out_h * rx->dec_width / rx->dec_height;
		/* Keep the NV12 plane's destination even-sized; the black primary
		 * plane fills the remaining letterbox/pillarbox area. */
		rect_w &= ~1u;
		rect_h &= ~1u;
		if (!rect_w || !rect_h) {
			qcap2_video_sink_delete(sink);
			return -1;
		}
		qcap2_video_sink_set_drm_rect(sink, (out_w - rect_w) / 2,
			(out_h - rect_h) / 2, rect_w, rect_h);
		std::shared_ptr<qcap2_video_format_t> format(
			qcap2_video_format_new(), qcap2_video_format_delete);
		qcap2_video_format_set_property(format.get(), QCAP_COLORSPACE_TYPE_NV12,
			out_w, out_h, FALSE, mode_vrefresh(&rx->drm_output_mode));
		qcap2_video_sink_set_video_format(sink, format.get());

		QRESULT qres = qcap2_video_sink_start(sink);
		if (qres != QCAP_RS_SUCCESSFUL) {
			LOGE("qcap2_video_sink_start(plane %u) failed, qres=%d",
				drm_plane_id, (int)qres);
			qcap2_video_sink_delete(sink);
			return -1;
		}

		rx->vdec_sink = sink;
		rx->vdec_sink_started = true;
		LOGI("%s VProcSS scaler: source %ux%u -> destination %ux%u at (%u,%u), HDMI %ux%u@%.2f (aspect preserved)",
		     rx->media_source_name, rx->dec_width, rx->dec_height, rect_w, rect_h,
		     (out_w - rect_w) / 2, (out_h - rect_h) / 2,
		     out_w, out_h, mode_vrefresh(&rx->drm_output_mode));
		LOGI("ALLEGRO2 decoder output is displayed on DRM plane %u", drm_plane_id);
		return 0;
	}

	/* Stop decoder callbacks before calling this helper. The next startup must
	 * re-read EDID and recalculate the VProcSS rectangle for the new source. */
	static void stop_vdec_display(zst_dante_rx_t* rx) {
		std::lock_guard<std::mutex> display_guard(rx->display_lock);
		if (rx->vdec_sink) {
			if (rx->vdec_sink_started) qcap2_video_sink_stop(rx->vdec_sink);
			qcap2_video_sink_delete(rx->vdec_sink);
			rx->vdec_sink = nullptr;
		}
		rx->vdec_sink_started = false;
		stop_drm_output(rx);
	}

	/* QCAP owns Dante control/RTP/depayload in --use-qcap mode. Keep the
	 * application-owned decoder and HDMI audio output so both receive only
	 * callback-lifetime data that is copied into their normal buffers. */
	static zst_dante_rx_t* start_qcap_media_rx(bool display_enabled) {
		zst_dante_rx_t* rx = new zst_dante_rx_t();
		rx->decode_enabled = true;
		rx->display_enabled = display_enabled;
		rx->video_stats = (rx_video_stats_t*)calloc(1, sizeof(*rx->video_stats));
		rx->audio_stats = (rx_audio_stats_t*)calloc(1, sizeof(*rx->audio_stats));
		if (!rx->video_stats || !rx->audio_stats) {
			free(rx->video_stats);
			free(rx->audio_stats);
			delete rx;
			return nullptr;
		}
		rx->video_stats->rx = rx;
		rx->audio_stats->rx = rx;
		if (rx_h264_parser_create(rx) != 0) {
			free(rx->video_stats); free(rx->audio_stats); delete rx;
			return nullptr;
		}
		if (start_hdmi_audio_sink(rx) != 0)
			LOGW("HDMI audio unavailable; continuing with video only");
		return rx;
	}

	static void stop_qcap_media_rx(zst_dante_rx_t* rx) {
		if (!rx) return;
		stop_vdec_decoder(rx);
		rx_h264_parser_destroy(rx);
		if (rx->extra_data) free(rx->extra_data);
		stop_vdec_display(rx);
		stop_hdmi_audio_sink(rx);
		free(rx->video_stats);
		free(rx->audio_stats);
		delete rx;
	}

	/* Use QCAP's media notification as a fallback only. h26xparse supplies the
	 * active in-band SPS/PPS avcC before the decoder is initialized. */
	static int start_qcap_decoder_from_format(zst_dante_rx_t* rx,
		uint32_t width, uint32_t height, double fps) {
		if (!rx || width == 0 || height == 0) return -1;
		std::lock_guard<std::mutex> decoder_guard(rx->decoder_lock);
		if (rx->vdec_started) return 0;
		if (!rx->dec_width) rx->dec_width = width;
		if (!rx->dec_height) rx->dec_height = height;
		if (fps >= 1.0 && fps <= 240.0) rx->dec_fps = fps;
		LOGI("QCAP format fallback %ux%u @ %.1f fps; awaiting in-band SPS/PPS caps",
		     width, height, rx->dec_fps);
		return 0;
	}

	static QRETURN qcap_dante_connected_callback(PVOID /*client*/, UINT /*session*/,
		ULONG video_format, ULONG width, ULONG height, BOOL /*interleaved*/, double fps,
		ULONG audio_format, ULONG audio_channels, ULONG audio_bits, ULONG audio_rate, PVOID user_data) {
		qcap_dante_rx_t* qcap_rx = (qcap_dante_rx_t*)user_data;
		zst_dante_rx_t* rx = qcap_rx ? qcap_rx->media_rx : nullptr;
		LOGI("QCAP media connected: video format=%u %ux%u@%.2f, audio format=%u %uch %ubit %uHz",
			(unsigned)video_format, (unsigned)width, (unsigned)height, fps,
			(unsigned)audio_format, (unsigned)audio_channels, (unsigned)audio_bits, (unsigned)audio_rate);
		if (video_format != QCAP_ENCODER_FORMAT_H264) {
			LOGE("QCAP media connected with unsupported video format=%u", (unsigned)video_format);
			return QCAP_RT_OK;
		}
		if (start_qcap_decoder_from_format(rx, width, height, fps) != 0)
			LOGE("QCAP media decoder initialization failed");
		return QCAP_RT_OK;
	}

	static QRETURN qcap_dante_fail_callback(PVOID /*client*/, UINT session,
		QRESULT status, DWORD error_code, PVOID /*user_data*/) {
		LOGE("QCAP broadcast client failed: session=%u status=%d error=%u",
			(unsigned)session, (int)status, (unsigned)error_code);
		return QCAP_RT_OK;
	}

	static QRETURN qcap_dante_video_callback(PVOID /*client*/, UINT /*session*/,
		double sample_time, BYTE* stream, ULONG stream_len, BOOL is_keyframe, PVOID user_data) {
		qcap_dante_rx_t* qcap_rx = (qcap_dante_rx_t*)user_data;
		zst_dante_rx_t* rx = qcap_rx ? qcap_rx->media_rx : nullptr;
		if (!rx || !rx->video_stats) return QCAP_RT_OK;
		qcap2_rcbuffer_t* rcbuf = qcap2_rcbuffer_cast(stream, stream_len);
		qcap2_av_packet_t* packet = rcbuf
			? (qcap2_av_packet_t*)qcap2_rcbuffer_lock_data(rcbuf) : nullptr;
		uint8_t* data = nullptr;
		int data_size = 0;
		if (packet) qcap2_av_packet_get_buffer(packet, &data, &data_size);
		if (!packet || !data || data_size <= 0) {
			if (rcbuf && packet) qcap2_rcbuffer_unlock_data(rcbuf);
			LOGW("QCAP video callback received an invalid rc-buffer");
			return QCAP_RT_OK;
		}
		rx->video_stats->total_packets++;
		rx->video_stats->total_bytes += (uint64_t)data_size;
		uint64_t pts = sample_time > 0.0 ? (uint64_t)(sample_time * 1000000000.0) : 0;
		if (rx->video_stats->first_pts == 0 && pts > 0) rx->video_stats->first_pts = pts;
		rx->video_stats->last_pts = pts;
		if (is_keyframe) rx->video_stats->keyframes++;

		/* rx_video_feed_decoder copies into a packet-pool before submit. It also
		 * feeds the separate parser observer while this callback data is valid. */
		rx_video_feed_decoder(rx->video_stats, data, (size_t)data_size,
			is_keyframe ? ZST_BUFFER_FLAG_KEYFRAME : 0, pts);
		qcap2_rcbuffer_unlock_data(rcbuf);
		return QCAP_RT_OK;
	}

	static QRETURN qcap_dante_audio_callback(PVOID /*client*/, UINT /*session*/,
		double sample_time, BYTE* stream, ULONG stream_len, PVOID user_data) {
		qcap_dante_rx_t* qcap_rx = (qcap_dante_rx_t*)user_data;
		zst_dante_rx_t* rx = qcap_rx ? qcap_rx->media_rx : nullptr;
		if (!rx || !rx->audio_stats) return QCAP_RT_OK;
		qcap2_rcbuffer_t* rcbuf = qcap2_rcbuffer_cast(stream, stream_len);
		qcap2_av_packet_t* packet = rcbuf
			? (qcap2_av_packet_t*)qcap2_rcbuffer_lock_data(rcbuf) : nullptr;
		uint8_t* data = nullptr;
		int data_size = 0;
		if (packet) qcap2_av_packet_get_buffer(packet, &data, &data_size);
		if (!packet || !data || data_size <= 0) {
			if (rcbuf && packet) qcap2_rcbuffer_unlock_data(rcbuf);
			LOGW("QCAP audio callback received an invalid rc-buffer");
			return QCAP_RT_OK;
		}
		/* QCAP Dante TX emits stereo PCM S32LE: DanteServer consumes int32_t
		 * internally and its software audio encoder performs source widening. */
		if ((data_size % (int)(2 * sizeof(int32_t))) != 0) {
			LOGW("QCAP audio packet has invalid stereo S32LE size: %d", data_size);
			qcap2_rcbuffer_unlock_data(rcbuf);
			return QCAP_RT_OK;
		}
		uint64_t pts = sample_time > 0.0 ? (uint64_t)(sample_time * 1000000000.0) : 0;
		rx->audio_stats->total_periods++;
		rx->audio_stats->total_bytes += (uint64_t)data_size;
		rx->audio_stats->total_samples += (uint64_t)data_size / (2 * sizeof(int32_t));
		rx->audio_stats->sample_rate = 48000;
		rx->audio_stats->channels = 2;
		rx->audio_stats->format = ZST_AUDIO_FMT_S32LE;
		if (rx->audio_stats->first_pts == 0 && pts > 0) rx->audio_stats->first_pts = pts;
		rx->audio_stats->last_pts = pts;

		zst_buffer_t buffer = {};
		buffer.memory.data = data;
		buffer.memory.size = (size_t)data_size;
		buffer.pts = pts;
		rx_audio_playback(rx, &buffer, nullptr);
		qcap2_rcbuffer_unlock_data(rcbuf);
		return QCAP_RT_OK;
	}

	static void rx_video_feed_decoder(rx_video_stats_t* stats,
	                                   const uint8_t* data, size_t data_size,
	                                   uint32_t flags, uint64_t pts) {
		zst_dante_rx_t* rx = stats->rx;
		if (!rx || !data || data_size < 4)
			return;
		if (rx->decoder_initializing.load(std::memory_order_acquire)) {
			rx->decoder_dropped_packets++;
			return;
		}
		if (flags & ZST_BUFFER_FLAG_DROP) {
			if (rx->decode_enabled) {
				std::lock_guard<std::mutex> decoder_guard(rx->decoder_lock);
				rx->decoder_dropped_packets++;
				if (rx->vdec_started) {
					qcap2_video_decoder_notify_discontinuity(rx->vdec);
				}
			}
			return;
		}
		/* The scheduler has multiple workers. Serialize file writes and decoder
		 * initialization/input even when upstream pushes overlap. */
		if (rx->decode_enabled && !rx->h264_input_parsed) {
			zst_result_t parse_result = rx_h264_parser_push(rx, data, data_size, pts);
			if (parse_result != ZST_OK) {
				static std::atomic<uint64_t> parser_errors{0};
				if (parser_errors.fetch_add(1, std::memory_order_relaxed) < 8)
					LOGW("h26xparse config observer rejected input AU (%zu bytes)", data_size);
			}
		}
		std::lock_guard<std::mutex> decoder_guard(rx->decoder_lock);
		if (rx->decoder_initializing.load(std::memory_order_acquire)) {
			rx->decoder_dropped_packets++;
			return;
		}
		if (rx->reconstructed_file) {
			if (fwrite(data, 1, data_size, rx->reconstructed_file) != data_size ||
			    fflush(rx->reconstructed_file) != 0) {
				LOGE("Failed to write reconstructed H.264 AU to %s",
				     rx->reconstructed_path.c_str());
				fclose(rx->reconstructed_file);
				rx->reconstructed_file = nullptr;
			}
		}
		if (!rx->decode_enabled)
			return;
		++rx->decoder_input_packets;

		/* The upstream zst_rtp_depayloader never sets ZST_BUFFER_FLAG_KEYFRAME
		 * on its output buffers, so the buffer flag cannot gate decoder
		 * startup.  Detect the actual NAL content instead. */
		int has_idr = 0;
		for (size_t i = 0; i + 3 < data_size; i++) {
			if (data[i] == 0 && data[i + 1] == 0 &&
			    (data[i + 2] == 1 || (i + 4 <= data_size && data[i + 2] == 0 && data[i + 3] == 1))) {
				size_t off = (data[i + 2] == 1) ? i + 3 : i + 4;
				if (off < data_size) {
					uint8_t nal_type = data[off] & 0x1F;
					if (nal_type == 5) has_idr = 1;
				}
			}
		}
		int is_keyframe = has_idr || (flags & ZST_BUFFER_FLAG_KEYFRAME);

		/* Decoder input is submitted directly; failed submissions are dropped. */
		if (rx->vdec_started) {
			if (rx->decoder_wait_for_idr.load(std::memory_order_acquire)) {
				if (!is_keyframe) {
					rx->decoder_dropped_packets++;
					return;
				}
				rx->decoder_wait_for_idr.store(false, std::memory_order_release);
				LOGI("ALLEGRO2 decoder accepting input from IDR after asynchronous initialization");
			}
			submit_vdec_packet(rx, data, data_size, is_keyframe != 0, pts);
			return;
		}

		/* h26xparse publishes avcC once an in-band slice selects its SPS/PPS.
		 * Wait for both that caps update and an IDR, without rewriting this AU. */
		if (!rx->extra_data || rx->extra_data_size <= 0 || !rx->dec_width ||
		    !rx->dec_height || !is_keyframe) {
			uint64_t waited = stats->no_sps_count.fetch_add(1, std::memory_order_relaxed) + 1;
			if (waited % 300 == 0)
				LOGW("ALLEGRO2 decoder waiting for h26xparse SPS/PPS caps and IDR (%lu video AUs)",
				     (unsigned long)stats->total_packets.load());
			return;
		}
		stats->no_sps_count.store(0, std::memory_order_relaxed);

		LOGI("ALLEGRO2 decoder initialization scheduled from h26xparse caps: %ux%u @ %.1f fps, avcC=%d bytes",
		     rx->dec_width, rx->dec_height, rx->dec_fps, rx->extra_data_size);
		start_vdec_decoder_async(rx);
	}

	static void* dante_rx_bus_worker(void* arg) {
		zst_dante_rx_t* rx = (zst_dante_rx_t*)arg;
		uint64_t last_stat_time = 0;
		uint64_t last_vpkts = 0;
		uint64_t last_vbytes = 0;
		uint64_t last_apds = 0;
		uint64_t last_abytes = 0;
		uint64_t last_rtp_in = 0;
		uint64_t last_decoded = 0;

		while (rx->running) {
			zst_event_t* ev = nullptr;
			if (zst_bus_pop(rx->pipe->bus, &ev, 100) == ZST_OK && ev) {
				if (ev->type == ZST_EVENT_DANTE_FLOW_CREATED) {
					const zst_dante_flow_t* f = &ev->as.dante_flow.flow;
					LOGI("[DANTE BUS] RX video flow CREATED: idx=%u ch=%u port=%u from=%s (%s)",
						f->flow_index, f->channel_index, f->port,
						f->transmitter_address ? f->transmitter_address : "?",
						f->transport == ZST_DANTE_FLOW_MULTICAST ? "multicast" : "unicast");
					zst_dante_video_coordinator_apply_flow(rx->coordinator, f);
					if (rx->auto_mode)
						rx->auto_flow_count = zst_dante_video_coordinator_get_flow_count(rx->coordinator);
				} else if (ev->type == ZST_EVENT_DANTE_FLOW_DELETED) {
					const zst_dante_flow_t* f = &ev->as.dante_flow.flow;
					LOGI("[DANTE BUS] RX video flow DELETED: idx=%u", f->flow_index);
					zst_dante_video_coordinator_remove_flow(rx->coordinator, f);
					if (rx->auto_mode)
						rx->auto_flow_count = zst_dante_video_coordinator_get_flow_count(rx->coordinator);
				} else if (ev->type == ZST_EVENT_DANTE_CONNECTED) {
					LOGI("[DANTE BUS] Dante session CONNECTED to DVR socket");
				} else if (ev->type == ZST_EVENT_WARNING) {
					LOGW("[DANTE BUS] Warning: %s", ev->as.warning.message ? ev->as.warning.message : "");
				} else if (ev->type == ZST_EVENT_ERROR) {
					LOGE("[DANTE BUS] Error: %s", ev->as.error.message ? ev->as.error.message : "");
				}
				zst_event_destroy(ev);
			}
			/* Several device/decoder statistics query live pipeline locks. A
			 * stalled sink can block those getters indefinitely, which would
			 * prevent this worker from handling FLOW_DELETED. In auto mode the
			 * bus must remain dedicated to routing events. */
			if (rx->auto_mode) continue;

			struct timespec ts;
			clock_gettime(CLOCK_MONOTONIC, &ts);
			uint64_t now_ms = (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
			if (last_stat_time == 0) {
				last_stat_time = now_ms;
				continue;
			}

			uint64_t interval_ms = rx->stat_interval_sec * 1000;
			if (now_ms - last_stat_time >= interval_ms) {
				double dt_sec = (double)(now_ms - last_stat_time) / 1000.0;
				last_stat_time = now_ms;

				/* Unbuffered heartbeat: if the worker wedges inside one of
				 * the live-pipeline getters below, the last tag pinpoints it.
				 * stderr is unbuffered, so these lines land even when stdout
				 * block buffering holds back the regular statistics. */
				fprintf(stderr, "[RX-HB %llu] stats-begin\n",
				        (unsigned long long)now_ms);

				uint64_t cur_vpkts = rx->video_stats ? rx->video_stats->total_packets.load() : 0;
				uint64_t cur_vbytes = rx->video_stats ? rx->video_stats->total_bytes.load() : 0;
				uint64_t cur_keyframes = rx->video_stats ? rx->video_stats->keyframes.load() : 0;
				uint64_t cur_sps_pps = rx->video_stats ? rx->video_stats->sps_pps.load() : 0;
				uint64_t cur_idr = rx->video_stats ? rx->video_stats->idr_slices.load() : 0;
				uint64_t cur_nonidr = rx->video_stats ? rx->video_stats->non_idr_slices.load() : 0;
				uint64_t cur_nosc = rx->video_stats ? rx->video_stats->no_sc.load() : 0;
				uint64_t cur_nalother = rx->video_stats ? rx->video_stats->nal_other.load() : 0;

				uint64_t delta_vpkts = cur_vpkts - last_vpkts;
				uint64_t delta_vbytes = cur_vbytes - last_vbytes;
				last_vpkts = cur_vpkts;
				last_vbytes = cur_vbytes;

				double fps = dt_sec > 0 ? (double)delta_vpkts / dt_sec : 0.0;
				double mbps = dt_sec > 0 ? ((double)delta_vbytes * 8.0) / (dt_sec * 1000000.0) : 0.0;

				uint64_t cur_apds = rx->audio_stats ? rx->audio_stats->total_periods.load() : 0;
				uint64_t cur_abytes = rx->audio_stats ? rx->audio_stats->total_bytes.load() : 0;
				uint64_t cur_asamples = rx->audio_stats ? rx->audio_stats->total_samples.load() : 0;
				uint32_t audio_rate = rx->audio_stats ? rx->audio_stats->sample_rate.load() : 0;
				uint32_t audio_channels = rx->audio_stats ? rx->audio_stats->channels.load() : 0;
				uint32_t audio_format = rx->audio_stats ? rx->audio_stats->format.load() : 0;
				uint64_t delta_apds = cur_apds - last_apds;
				uint64_t delta_abytes = cur_abytes - last_abytes;
				last_apds = cur_apds;
				last_abytes = cur_abytes;
				double a_rate = dt_sec > 0 ? (double)delta_apds / dt_sec : 0.0;
				double a_mbps = dt_sec > 0 ? ((double)delta_abytes * 8.0) / (dt_sec * 1000000.0) : 0.0;

				uint32_t underflows = rx->rx_dep_src ? get_uint_prop(rx->rx_dep_src, "underflow-count") : 0;
				uint32_t overruns = rx->rx_dep_src ? get_uint_prop(rx->rx_dep_src, "overrun-count") : 0;
				fprintf(stderr, "[RX-HB %llu] dep-getter-done\n",
				        (unsigned long long)now_ms);
				uint32_t active_flows = zst_dante_video_coordinator_get_flow_count(rx->coordinator);
				fprintf(stderr, "[RX-HB %llu] flows-getter-done\n",
				        (unsigned long long)now_ms);

				uint64_t rtp_in = get_uint_prop(rx->coordinator, "rx-in-packets");
				uint64_t rtp_lost = get_uint_prop(rx->coordinator, "rx-lost-packets");
				uint64_t probe_rtp_in = get_uint_prop(rx->coordinator,
					ZST_DANTE_VIDEO_COORDINATOR_PROP_RX_PROBE_IN_PACKETS);
				uint64_t probe_rtp_lost = get_uint_prop(rx->coordinator,
					ZST_DANTE_VIDEO_COORDINATOR_PROP_RX_PROBE_LOST_PACKETS);
				uint64_t rtp_late = get_uint_prop(rx->coordinator,
					ZST_DANTE_VIDEO_COORDINATOR_PROP_RX_LATE_PACKETS);
				uint64_t rtp_old = get_uint_prop(rx->coordinator, "rx-old-packets");
				uint64_t rtp_dup = get_uint_prop(rx->coordinator, "rx-dup-packets");
				uint64_t parse_fail = get_uint_prop(rx->coordinator, "rx-parse-fail");
				uint64_t dep_in = get_uint_prop(rx->coordinator, "rx-dep-in-packets");
				uint64_t dep_dropped = get_uint_prop(rx->coordinator, "rx-dep-dropped");
				fprintf(stderr, "[RX-HB %llu] rtp-getter-done\n",
				        (unsigned long long)now_ms);
				fprintf(stderr, "[RX-HB %llu] coord-getter-done\n",
				        (unsigned long long)now_ms);
				uint64_t udp_in = 0;
				uint64_t udp_qoverflow = 0;
				uint64_t udp_qdepth = 0;
				if (zst_element_t* rx_udp_src = zst_dante_video_coordinator_get_rx_udp_source(rx->coordinator, 0)) {
					udp_in = get_uint_prop(rx_udp_src, "rtp-packets");
					udp_qoverflow = get_uint_prop(rx_udp_src, "queue-overflow");
					udp_qdepth = get_uint_prop(rx_udp_src, "queue-depth");
				}
				double rtp_rate = dt_sec > 0 ? (double)(rtp_in - last_rtp_in) / dt_sec : 0.0;
				last_rtp_in = rtp_in;

				uint64_t dec_input = rx->decoder_input_packets.load();
				uint64_t dec_pushed = rx->decoder_pushed_packets.load();
				uint64_t dec_dropped = rx->decoder_dropped_packets.load();
				uint64_t transport_dropped = rx->transport_dropped_packets.load();
				uint64_t pool_dropped = rx->packet_pool_dropped_packets.load();
				uint64_t push_failed = rx->decoder_push_failed_packets.load();
				uint64_t recovery_skipped = 0;
				fprintf(stderr, "[RX-HB %llu] decoder-getter-enter\n",
				        (unsigned long long)now_ms);
				{
					std::lock_guard<std::mutex> decoder_guard(rx->decoder_lock);
					if (rx->vdec)
						recovery_skipped = qcap2_video_decoder_get_recovery_skipped_packets(rx->vdec);
				}
				uint64_t decoded = rx->decoded_frames.load();
				double decoded_fps = dt_sec > 0 ? (decoded - last_decoded) / dt_sec : 0.0;
				last_decoded = decoded;
				double dec_drop_rate = dec_input > 0
					? 100.0 * (double)dec_dropped / (double)dec_input : 0.0;
				fprintf(stderr, "[RX-HB %llu] stats-print dec=%llu\n",
				        (unsigned long long)now_ms, (unsigned long long)decoded);
				LOGI("[DANTE RX DECODER] input=%lu pushed=%lu dropped=%lu (%.2f%%: transport=%lu, pool=%lu, push=%lu, recovery=%lu) decoded=%lu (%.1f fps)",
					(unsigned long)dec_input, (unsigned long)dec_pushed,
					(unsigned long)dec_dropped, dec_drop_rate, (unsigned long)transport_dropped,
					(unsigned long)pool_dropped, (unsigned long)push_failed,
					(unsigned long)recovery_skipped,
					(unsigned long)decoded, decoded_fps);

				if (active_flows > 0 || cur_vpkts > 0) {
					LOGI("[DANTE RX STATS] flows=%u | VIDEO: %lu AUs (%.2f MB, %.1f fps, %.2f Mbps) | RTP: %lu udp-in, %lu reord-in (%.0f/s), lost=%lu, late=%lu, old=%lu, dup=%lu, parse-fail=%lu, dep-in=%lu, dep-drop=%lu, qof=%lu, qd=%lu | PROBE port+10: reord-in=%lu lost=%lu | AUDIO: %u Hz, %u ch, fmt=%u, %lu periods (%.2f MB, %.2f Mbps, %lu samples, %.1f p/s, underflow=%u, overrun=%u)",
						active_flows,
						(unsigned long)cur_vpkts,
						(double)cur_vbytes / (1024.0 * 1024.0),
						fps, mbps,
						(unsigned long)udp_in,
						(unsigned long)rtp_in, rtp_rate,
						(unsigned long)rtp_lost,
						(unsigned long)rtp_late,
						(unsigned long)rtp_old,
						(unsigned long)rtp_dup,
						(unsigned long)parse_fail,
						(unsigned long)dep_in,
						(unsigned long)dep_dropped,
						(unsigned long)udp_qoverflow,
						(unsigned long)udp_qdepth,
						(unsigned long)probe_rtp_in,
						(unsigned long)probe_rtp_lost,
						audio_rate, audio_channels, audio_format,
						(unsigned long)cur_apds,
						(double)cur_abytes / (1024.0 * 1024.0),
						a_mbps,
						(unsigned long)cur_asamples,
						a_rate,
						underflows, overruns);
				} else {
					LOGI("[DANTE RX STATS] flows=0 (waiting for Dante Controller routing...) | VIDEO: 0 pkts | AUDIO: %lu periods (%.2f MB, %lu samples, underflow=%u, overrun=%u)",
						(unsigned long)cur_apds,
						(double)cur_abytes / (1024.0 * 1024.0),
						(unsigned long)cur_asamples,
						underflows, overruns);
				}
			}
		}
		return nullptr;
	}

	static zst_dante_rx_t* start_dante_rx(const std::string& dvr_socket,
	                                     const std::string& dep_name,
	                                     const std::string& channels,
	                                     uint32_t sample_rate,
	                                     uint32_t stat_interval,
	                                     const std::string& reconstructed_path,
                                     bool decode_enabled = true,
                                     bool rtp_stats_only = false,
                                     bool direct_mode = false,
                                     const std::string& direct_peer = "",
                                     const std::string& direct_local = "0.0.0.0",
                                     uint16_t direct_port = 16490,
                                     bool display_enabled = true,
                                     uint32_t reorder_window = 1,
	                                     uint32_t reorder_timeout_ms = 1,
	                                     bool auto_mode = false)
	{
		zst_dante_rx_t* rx = new zst_dante_rx_t();
		rx->dvr_socket = dvr_socket;
		rx->dep_name = dep_name;
		rx->channels = channels;
		rx->sample_rate = sample_rate;
		rx->stat_interval_sec = stat_interval > 0 ? stat_interval : 3;
		rx->reorder_window = reorder_window;
		rx->reorder_timeout_ms = reorder_timeout_ms;
		rx->reconstructed_path = reconstructed_path;
		rx->decode_enabled = decode_enabled;
		rx->display_enabled = display_enabled;
		rx->auto_mode = auto_mode;
		rx->direct_mode = direct_mode;
		rx->direct_transmitter = direct_peer;
		rx->direct_local = direct_local.empty() ? "0.0.0.0" : direct_local;
		rx->direct_port = direct_port;

		struct timespec ts;
		clock_gettime(CLOCK_MONOTONIC, &ts);
		rx->start_time_ms = (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;

		LOGI("Initializing Dante RX pipeline (zstreamer)...");
		LOGI("  DVR Socket: %s", rx->dvr_socket.c_str());
		LOGI("  DEP SHM:    %s", rx->dep_name.c_str());
		LOGI("  Channels:   %s", rx->channels.c_str());
		LOGI("  Audio Rate: %u Hz", rx->sample_rate);

		rx->pipe = zst_pipeline_create();
		if (!rx->pipe) {
			LOGE("zst_pipeline_create() failed");
			delete rx;
			return nullptr;
		}

		/* 1. Dante DVR Session (skipped in direct RTP mode) */
		if (!direct_mode) {
			rx->session = zst_dante_session_create(rx->dvr_socket.c_str());
			if (!rx->session) {
				LOGE("zst_dante_session_create(%s) failed", rx->dvr_socket.c_str());
				zst_pipeline_destroy(rx->pipe);
				delete rx;
				return nullptr;
			}
			set_uint(rx->session, "tx-video-channels", 0);
			set_uint(rx->session, "rx-video-channels", 1);
		}

		/* 2. Dante Video Coordinator */
		rx->coordinator = zst_dante_video_coordinator_create();
		if (!rx->coordinator) {
			LOGE("zst_dante_video_coordinator_create() failed");
			if (rx->session) zst_element_destroy(rx->session);
			zst_pipeline_destroy(rx->pipe);
			delete rx;
			return nullptr;
		}
		if (rx->session)
			zst_dante_video_coordinator_attach_session(rx->coordinator, rx->session);
		/* Reorder tolerance trades a little latency for AU completeness.
		 * Must be programmed before the first flow is created. */
		set_uint(rx->coordinator, ZST_DANTE_VIDEO_COORDINATOR_PROP_REORDER_WINDOW, rx->reorder_window);
		set_uint(rx->coordinator, ZST_DANTE_VIDEO_COORDINATOR_PROP_REORDER_TIMEOUT_MS, rx->reorder_timeout_ms);
		LOGI("  Reorder: window=%u timeout=%ums", rx->reorder_window, rx->reorder_timeout_ms);
		rx->rx_video_pad = zst_dante_video_coordinator_request_rx_output_pad(rx->coordinator, 0);
		if (!rx->rx_video_pad) {
			LOGE("zst_dante_video_coordinator_request_rx_output_pad() failed");
			zst_element_destroy(rx->coordinator);
			zst_element_destroy(rx->session);
			zst_pipeline_destroy(rx->pipe);
			delete rx;
			return nullptr;
		}

		/* 3. Feed depayloaded Annex-B access units directly to the QCAP bridge. */
		rx->rx_video_sink = create_rx_video_sink(&rx->video_stats);
		if (!rx->rx_video_sink) {
			LOGE("create_rx_video_sink() failed");
			zst_element_destroy(rx->coordinator);
			zst_element_destroy(rx->session);
			zst_pipeline_destroy(rx->pipe);
			delete rx;
			return nullptr;
		}
		rx->video_stats->rx = rx;
		if (rx_h264_parser_create(rx) != 0) {
			zst_element_destroy(rx->rx_video_sink);
			zst_element_destroy(rx->coordinator);
			if (rx->session) zst_element_destroy(rx->session);
			zst_pipeline_destroy(rx->pipe);
			delete rx;
			return nullptr;
		}
		zst_pad_link(rx->rx_video_pad, zst_element_get_pad(rx->rx_video_sink, "sink"));

		/* 4. Dante DEP audio is excluded from RTP-only measurement mode. */
		if (!direct_mode && !rtp_stats_only) {
			rx->rx_dep_src = zst_dante_dep_audio_source_create();
			if (!rx->rx_dep_src) {
				LOGE("zst_dante_dep_audio_source_create() failed");
				rx_h264_parser_destroy(rx);
				zst_element_destroy(rx->rx_video_sink);
				zst_element_destroy(rx->coordinator);
				if (rx->session) zst_element_destroy(rx->session);
				zst_pipeline_destroy(rx->pipe);
				delete rx;
				return nullptr;
			}
			set_str(rx->rx_dep_src, "shm-name", rx->dep_name.c_str());
			set_str(rx->rx_dep_src, "channels", rx->channels.c_str());
			set_uint(rx->rx_dep_src, "expected-sample-rate", rx->sample_rate);

			/* 5. RX Audio Sink */
			rx->rx_audio_sink = create_rx_audio_sink(&rx->audio_stats);
			if (!rx->rx_audio_sink) {
				LOGE("create_rx_audio_sink() failed");
				zst_element_destroy(rx->rx_dep_src);
				rx_h264_parser_destroy(rx);
				zst_element_destroy(rx->rx_video_sink);
				zst_element_destroy(rx->coordinator);
				if (rx->session) zst_element_destroy(rx->session);
				zst_pipeline_destroy(rx->pipe);
				delete rx;
				return nullptr;
			}
			rx->audio_stats->rx = rx;
			zst_pad_link(zst_element_get_pad(rx->rx_dep_src, "src"),
			             zst_element_get_pad(rx->rx_audio_sink, "sink"));
			if (!auto_mode && start_hdmi_audio_sink(rx) != 0) {
				LOGW("HDMI audio unavailable; continuing with video only");
			}
		}

		/* Add elements to pipeline */
		zst_pipeline_add(rx->pipe, rx->coordinator);
		zst_pipeline_add(rx->pipe, rx->rx_video_sink);
		if (!direct_mode && !rtp_stats_only) {
			zst_pipeline_add(rx->pipe, rx->rx_dep_src);
			zst_pipeline_add(rx->pipe, rx->rx_audio_sink);
		}

		/* Clocks and pipeline state */
		if (!rx->reconstructed_path.empty()) {
			rx->reconstructed_file = fopen(rx->reconstructed_path.c_str(), "wb");
			if (!rx->reconstructed_file) {
				LOGE("Failed to open reconstructed bitstream output: %s",
				     rx->reconstructed_path.c_str());
				return nullptr;
			}
			LOGI("Writing reconstructed Annex-B H.264 AUs to %s",
			     rx->reconstructed_path.c_str());
		}
		rx->clock = zst_clock_system_create();
		zst_pipeline_set_clock(rx->pipe, rx->clock);
		zst_pipeline_set_state(rx->pipe, ZST_STATE_PLAYING);
		if (rx->session) {
			rx->session->bus = rx->pipe->bus;
			if (zst_element_set_state(rx->session, ZST_STATE_PLAYING) != ZST_OK) {
				LOGW("Dante session start failed (check if /var/run/dante/dvr is active)");
			}
		}

		/* Multi-threaded scheduler */
		zst_scheduler_config_t cfg = {
			.mode = ZST_SCHEDULER_MULTI_THREAD,
			.worker_threads = 4
		};
		rx->sched = zst_scheduler_create(&cfg);
		zst_scheduler_attach(rx->sched, rx->pipe);
		pthread_create(&rx->sched_th, nullptr, (void* (*)(void*))zst_scheduler_run, rx->sched);

		/* Direct mode: synthesize a unicast H.264 RX flow (no Dante daemon). */
		if (direct_mode) {
			zst_dante_flow_t flow = {};
			flow.direction = ZST_DANTE_FLOW_RX;
			flow.transport = ZST_DANTE_FLOW_UNICAST;
			flow.flow_index = 0;
			flow.channel_index = 0;
			flow.port = rx->direct_port;
			flow.receiver_address = (char*)(rx->direct_local.c_str());
			flow.transmitter_address = (char*)(rx->direct_transmitter.c_str());
			flow.multicast_address = nullptr;
			snprintf(flow.video_subtype, sizeof(flow.video_subtype), "H264");
			zst_result_t r = zst_dante_video_coordinator_apply_flow(rx->coordinator, &flow);
			if (r != ZST_OK) {
				LOGE("direct RX flow apply failed, r=%d", (int)r);
			} else {
				LOGI("[DIRECT RX] listening on %s:%u from %s (loss-first reorder 1/1ms)",
				     rx->direct_local.c_str(), rx->direct_port,
				     rx->direct_transmitter.c_str());
			}
		}

		rx->running = true;
		pthread_create(&rx->bus_th, nullptr, dante_rx_bus_worker, rx);

		LOGI("Dante RX pipeline started successfully.");
		return rx;
	}

	static void stop_dante_rx(zst_dante_rx_t* rx) {
		if (!rx) return;
		LOGI("Stopping Dante RX pipeline...");
		rx->running = false;
		if (rx->bus_th) {
			pthread_join(rx->bus_th, nullptr);
			rx->bus_th = 0;
		}
		/* Stop scheduler before decoder teardown: scheduler workers submit input AUs. */
		if (rx->sched) {
			zst_scheduler_stop(rx->sched);
			pthread_join(rx->sched_th, nullptr);
			zst_scheduler_destroy(rx->sched);
			rx->sched = nullptr;
		}
		stop_hdmi_audio_sink(rx);

		/* Snapshot statistics before elements are destroyed */
		uint64_t vpkts = rx->video_stats ? rx->video_stats->total_packets.load() : 0;
		uint64_t vbytes = rx->video_stats ? rx->video_stats->total_bytes.load() : 0;
		uint64_t keyframes = rx->video_stats ? rx->video_stats->keyframes.load() : 0;
		uint64_t idr = rx->video_stats ? rx->video_stats->idr_slices.load() : 0;
		uint64_t sps_pps = rx->video_stats ? rx->video_stats->sps_pps.load() : 0;
		uint64_t apds = rx->audio_stats ? rx->audio_stats->total_periods.load() : 0;
		uint64_t asamples = rx->audio_stats ? rx->audio_stats->total_samples.load() : 0;
		uint64_t abytes = rx->audio_stats ? rx->audio_stats->total_bytes.load() : 0;
		uint64_t dec_frames = rx->decoded_frames.load();
		uint64_t dec_errors = rx->decoded_errors.load();
		uint64_t dec_input = rx->decoder_input_packets.load();
		uint64_t dec_pushed = rx->decoder_pushed_packets.load();
		uint64_t dec_dropped = rx->decoder_dropped_packets.load();
		uint64_t transport_dropped = rx->transport_dropped_packets.load();
		uint64_t pool_dropped = rx->packet_pool_dropped_packets.load();
		uint64_t push_failed = rx->decoder_push_failed_packets.load();
		uint64_t audio_errors = rx->audio_playback_errors.load();
		uint64_t audio_unsupported = rx->audio_unsupported_periods.load();
		uint64_t recovery_skipped = 0;
		{
			std::lock_guard<std::mutex> decoder_guard(rx->decoder_lock);
			if (rx->vdec)
				recovery_skipped = qcap2_video_decoder_get_recovery_skipped_packets(rx->vdec);
		}
		double dec_drop_rate = dec_input > 0
			? 100.0 * (double)dec_dropped / (double)dec_input : 0.0;

		stop_vdec_decoder(rx);
		if (rx->vdec_sink) {
			if (rx->vdec_sink_started)
				qcap2_video_sink_stop(rx->vdec_sink);
			qcap2_video_sink_delete(rx->vdec_sink);
			rx->vdec_sink = nullptr;
			rx->vdec_sink_started = false;
		}
		stop_drm_output(rx);

		if (rx->session) {
			zst_element_set_state(rx->session, ZST_STATE_NULL);
			zst_element_destroy(rx->session);
			rx->session = nullptr;
		}
		if (rx->reconstructed_file) {
			fclose(rx->reconstructed_file);
			rx->reconstructed_file = nullptr;
			LOGI("Wrote reconstructed Annex-B H.264 to %s", rx->reconstructed_path.c_str());
		}
		if (rx->pipe) {
			zst_pipeline_set_state(rx->pipe, ZST_STATE_NULL);
			zst_pipeline_destroy(rx->pipe);
			rx->pipe = nullptr;
		}
		/* Quiesce upstream before destroying the out-of-pipeline observer. */
		rx_h264_parser_destroy(rx);
		if (rx->extra_data) {
			free(rx->extra_data);
			rx->extra_data = nullptr;
			rx->extra_data_size = 0;
		}
		if (rx->clock) {
			zst_clock_unref(rx->clock);
			rx->clock = nullptr;
		}

		/* Print final summary report */
		struct timespec ts;
		clock_gettime(CLOCK_MONOTONIC, &ts);
		uint64_t end_time_ms = (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
		double total_sec = rx->start_time_ms > 0 ? (double)(end_time_ms - rx->start_time_ms) / 1000.0 : 0.0;
		double mbps = total_sec > 0 ? ((double)vbytes * 8.0) / (total_sec * 1000000.0) : 0.0;

		printf("\n================ Dante RX Final Summary ================\n");
		printf("  Duration:            %.2f s\n", total_sec);
		printf("  Video Packets:       %llu\n", (unsigned long long)vpkts);
		printf("  Video Data:          %.2f MB (avg %.2f Mbps)\n",
		       (double)vbytes / (1024.0 * 1024.0), mbps);
		printf("  Video Keyframes:     %llu\n", (unsigned long long)keyframes);
		printf("  Video IDR Slices:    %llu\n", (unsigned long long)idr);
		printf("  Video SPS/PPS NALs:  %llu\n", (unsigned long long)sps_pps);
		printf("  Decoder Input AUs:   %llu\n", (unsigned long long)dec_input);
		printf("  Decoder Pushed AUs:  %llu\n", (unsigned long long)dec_pushed);
		printf("  Decoder Dropped AUs: %llu (%.2f%%)\n",
		       (unsigned long long)dec_dropped, dec_drop_rate);
		printf("    Transport / Pool / Push / Recovery: %llu / %llu / %llu / %llu\n",
		       (unsigned long long)transport_dropped, (unsigned long long)pool_dropped,
		       (unsigned long long)push_failed, (unsigned long long)recovery_skipped);
		printf("  Decoded Frames:      %llu\n", (unsigned long long)dec_frames);
		printf("  Decode Errors:       %llu\n", (unsigned long long)dec_errors);
		printf("  Audio Periods:       %llu\n", (unsigned long long)apds);
		printf("  Audio Samples:       %llu (%.2f MB)\n",
		       (unsigned long long)asamples,
		       (double)abytes / (1024.0 * 1024.0));
		printf("  Audio Output Errors: %llu (unsupported periods: %llu)\n",
		       (unsigned long long)audio_errors, (unsigned long long)audio_unsupported);
		printf("========================================================\n\n");

		delete rx;
		LOGI("Dante RX pipeline stopped.");
	}

	static volatile sig_atomic_t g_stop_requested = 0;
	static void sig_handler(int sig) {
		(void)sig;
		g_stop_requested++;
		if (g_stop_requested > 1) {
			_exit(130);
		}
	}

	/* ── Ultra-Direct FPGA Passthrough Mode ──────────────────────────── */

	static inline float mode_vrefresh(const drmModeModeInfo* mode) {
		if (!mode || mode->htotal == 0 || mode->vtotal == 0) return 0.0f;
		return mode->clock * 1000.0f / (mode->htotal * mode->vtotal);
	}

	static bool is_valid_edid(const unsigned char* edid, size_t len) {
		static const unsigned char edid_header[8] = {
			0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00
		};
		if (!edid || len < 128) return false;
		if (memcmp(edid, edid_header, sizeof(edid_header)) != 0) return false;
		size_t blocks = len / 128;
		if (blocks > 2) blocks = 2;
		if (blocks == 0) return false;
		for (size_t b = 0; b < blocks; b++) {
			unsigned int sum = 0;
			for (size_t i = 0; i < 128; i++)
				sum += edid[b * 128 + i];
			if ((sum & 0xff) != 0) return false;
		}
		return true;
	}

	static const unsigned char g_fallback_edid[EDID_LEN] = {
		0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x61, 0x98, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12,
		0x03, 0x1c, 0x01, 0x03, 0x80, 0x59, 0x32, 0x78, 0x0a, 0xee, 0x91, 0xa3, 0x54, 0x4c, 0x99, 0x26,
		0x0f, 0x50, 0x54, 0x21, 0x08, 0x00, 0x71, 0x4f, 0x81, 0xc0, 0x81, 0x00, 0x81, 0x80, 0x95, 0x00,
		0xa9, 0xc0, 0xb3, 0x00, 0x01, 0x01, 0x02, 0x3a, 0x80, 0x18, 0x71, 0x38, 0x2d, 0x40, 0x58, 0x2c,
		0x45, 0x00, 0x20, 0xc2, 0x31, 0x00, 0x00, 0x1e, 0x00, 0x00, 0x00, 0xfc, 0x00, 0x58, 0x49, 0x4c,
		0x49, 0x4e, 0x58, 0x20, 0x48, 0x44, 0x4d, 0x49, 0x0a, 0x20, 0x00, 0x00, 0x00, 0xfd, 0x00, 0x18,
		0x4b, 0x0f, 0x8c, 0x3c, 0x00, 0x0a, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x00, 0x00, 0x00, 0x10,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x36,
		0x02, 0x03, 0x3d, 0x70, 0x5c, 0x61, 0x10, 0x1f, 0x21, 0x20, 0x5d, 0x5e, 0x5f, 0x60, 0x65, 0x66,
		0x62, 0x63, 0x64, 0x48, 0x67, 0x68, 0x69, 0x6b, 0x6a, 0x22, 0x3e, 0x04, 0x3e, 0x3d, 0x41, 0x43,
		0x45, 0x23, 0x09, 0x07, 0x07, 0x6d, 0x03, 0x0c, 0x00, 0x10, 0x00, 0x78, 0x3c, 0x20, 0x00, 0x60,
		0x01, 0x02, 0x03, 0x67, 0xd8, 0x5d, 0xc4, 0x01, 0x78, 0x80, 0x07, 0xe1, 0x0f, 0x02, 0x3a, 0x80,
		0x18, 0x71, 0x38, 0x2d, 0x40, 0x58, 0x2c, 0x45, 0x00, 0x20, 0xc2, 0x31, 0x00, 0x00, 0x1e, 0x08,
		0xe8, 0x00, 0x30, 0xf2, 0x70, 0x5a, 0x80, 0xb0, 0x58, 0x8a, 0x00, 0x20, 0xc2, 0x31, 0x00, 0x00,
		0x1e, 0x04, 0x74, 0x00, 0x30, 0xf2, 0x70, 0x5a, 0x80, 0xb0, 0x58, 0x8a, 0x00, 0x20, 0x52, 0x31,
		0x00, 0x00, 0x1e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2c
	};

	static bool read_connector_edid(int fd, uint32_t connector_id, unsigned char* out_edid, size_t max_len) {
		if (fd < 0 || connector_id == 0 || !out_edid || max_len < EDID_LEN) return false;
		std::shared_ptr<drmModeObjectProperties> props(
			drmModeObjectGetProperties(fd, connector_id, DRM_MODE_OBJECT_CONNECTOR), drmModeFreeObjectProperties);
		if (!props) return false;
		uint32_t prop_id = drm_property_id(fd, connector_id, DRM_MODE_OBJECT_CONNECTOR, "EDID");
		if (prop_id == 0) return false;
		for (uint32_t i = 0; i < props->count_props; i++) {
			if (props->props[i] == prop_id) {
				uint64_t blob_id = props->prop_values[i];
				if (blob_id == 0) break;
				std::shared_ptr<drmModePropertyBlobRes> blob(
					drmModeGetPropertyBlob(fd, blob_id), drmModeFreePropertyBlob);
				if (blob && blob->data && blob->length >= 128) {
					size_t copy_len = blob->length < max_len ? blob->length : max_len;
					memset(out_edid, 0, max_len);
					memcpy(out_edid, blob->data, copy_len);
					if (is_valid_edid(out_edid, copy_len))
						return true;
				}
				break;
			}
		}
		return false;
	}

	static int open_drm_uevent_socket() {
		int fd = socket(AF_NETLINK, SOCK_DGRAM, NETLINK_KOBJECT_UEVENT);
		if (fd < 0) return -1;
		int flags = fcntl(fd, F_GETFL, 0);
		if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
		flags = fcntl(fd, F_GETFD, 0);
		if (flags >= 0) fcntl(fd, F_SETFD, flags | FD_CLOEXEC);

		struct sockaddr_nl addr;
		memset(&addr, 0, sizeof(addr));
		addr.nl_family = AF_NETLINK;
		addr.nl_pid = (uint32_t)getpid();
		addr.nl_groups = 1;

		if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
			close(fd);
			return -1;
		}
		return fd;
	}

	static bool is_target_connector_uevent(const char* buf, ssize_t len) {
		bool is_drm = false;
		bool is_hotplug = false;
		bool connector_matched = false;
		bool has_connector_name = false;

		for (ssize_t off = 0; off < len; ) {
			const char* s = buf + off;
			size_t left = (size_t)(len - off);
			size_t n = strnlen(s, left);
			if (n == 0) {
				off++;
				continue;
			}
			if (strcmp(s, "SUBSYSTEM=drm") == 0) {
				is_drm = true;
			} else if (strcmp(s, "HOTPLUG=1") == 0) {
				is_hotplug = true;
			} else if (strncmp(s, "CONNECTOR=", 10) == 0) {
				has_connector_name = true;
				if (strcmp(s + 10, "HDMI-A-1") == 0)
					connector_matched = true;
			} else if (strncmp(s, "DEVPATH=", 8) == 0 || strncmp(s, "change@", 7) == 0) {
				if (strstr(s, "HDMI-A-1") != nullptr)
					connector_matched = true;
			}
			off += (ssize_t)n + 1;
		}
		return is_drm && is_hotplug && (!has_connector_name || connector_matched);
	}

	static bool find_matching_mode(drmModeConnector* connector, uint32_t width, uint32_t height,
	                               double framerate, bool interlaced, drmModeModeInfo* out_mode) {
		if (!connector || !out_mode) return false;
		/* Prefer CEA-861 2160p60 over a monitor's reduced-blanking 533.25 MHz mode.
		 * The HDMI TX PHY on SC6F0 is validated for the 594 MHz CEA timing. */
		const uint32_t preferred_clock =
			(width == 3840 && height == 2160 && !interlaced) ? 594000 : 0;
		drmModeModeInfo fallback = {};
		bool have_fallback = false;
		for (int i = 0; i < connector->count_modes; i++) {
			drmModeModeInfo* candidate = &connector->modes[i];
			bool mode_interlaced = (candidate->flags & DRM_MODE_FLAG_INTERLACE) != 0;
			double refresh = mode_vrefresh(candidate);
			bool refresh_matched = (framerate <= 0.0) ||
				(fabs(refresh - framerate) < 0.5) ||
				(candidate->vrefresh == (int)framerate);
			if (candidate->hdisplay == (int)width &&
				candidate->vdisplay == (int)height &&
				mode_interlaced == interlaced &&
				refresh_matched) {
				if (preferred_clock && candidate->clock == preferred_clock) {
					*out_mode = *candidate;
					return true;
				}
				if (!have_fallback) {
					fallback = *candidate;
					have_fallback = true;
				}
			}
		}
		if (have_fallback) {
			*out_mode = fallback;
			return true;
		}
		for (int i = 0; i < connector->count_modes; i++) {
			drmModeModeInfo* candidate = &connector->modes[i];
			bool mode_interlaced = (candidate->flags & DRM_MODE_FLAG_INTERLACE) != 0;
			if (candidate->hdisplay == (int)width &&
				candidate->vdisplay == (int)height &&
				mode_interlaced == interlaced) {
				*out_mode = *candidate;
				return true;
			}
		}
		return false;
	}

	static bool find_connector_crtc(int fd, drmModeRes* resources, drmModeConnector* connector,
	                                uint32_t* out_crtc_id, int* out_crtc_index) {
		if (!resources || !connector || !out_crtc_id || !out_crtc_index) return false;
		if (connector->encoder_id != 0) {
			std::shared_ptr<drmModeEncoder> encoder(
				drmModeGetEncoder(fd, connector->encoder_id), drmModeFreeEncoder);
			if (encoder && encoder->crtc_id != 0) {
				for (int i = 0; i < resources->count_crtcs; i++) {
					if (resources->crtcs[i] == encoder->crtc_id) {
						*out_crtc_id = encoder->crtc_id;
						*out_crtc_index = i;
						return true;
					}
				}
			}
		}
		for (int i = 0; i < connector->count_encoders; i++) {
			std::shared_ptr<drmModeEncoder> encoder(
				drmModeGetEncoder(fd, connector->encoders[i]), drmModeFreeEncoder);
			if (!encoder) continue;
			for (int j = 0; j < resources->count_crtcs; j++) {
				if (encoder->possible_crtcs & (1u << j)) {
					*out_crtc_id = resources->crtcs[j];
					*out_crtc_index = j;
					return true;
				}
			}
		}
		return false;
	}

	static uint32_t find_plane_by_type(int fd, int crtc_index, uint32_t plane_type) {
		std::shared_ptr<drmModePlaneRes> planes(drmModeGetPlaneResources(fd), drmModeFreePlaneResources);
		if (!planes) return 0;
		for (uint32_t i = 0; i < planes->count_planes; i++) {
			std::shared_ptr<drmModePlane> plane(drmModeGetPlane(fd, planes->planes[i]), drmModeFreePlane);
			if (!plane || !(plane->possible_crtcs & (1u << crtc_index))) continue;
			uint32_t type = drm_property_id(fd, plane->plane_id, DRM_MODE_OBJECT_PLANE, "type");
			std::shared_ptr<drmModeObjectProperties> props(
				drmModeObjectGetProperties(fd, plane->plane_id, DRM_MODE_OBJECT_PLANE), drmModeFreeObjectProperties);
			if (!type || !props) continue;
			for (uint32_t p = 0; p < props->count_props; ++p) {
				if (props->props[p] == type && props->prop_values[p] == plane_type) {
					return plane->plane_id;
				}
			}
		}
		return 0;
	}

	static uint32_t find_overlay_plane_yuyv(int fd, int crtc_index) {
		std::shared_ptr<drmModePlaneRes> planes(drmModeGetPlaneResources(fd), drmModeFreePlaneResources);
		if (!planes) return 0;
		for (uint32_t i = 0; i < planes->count_planes; i++) {
			std::shared_ptr<drmModePlane> plane(drmModeGetPlane(fd, planes->planes[i]), drmModeFreePlane);
			if (!plane || !(plane->possible_crtcs & (1u << crtc_index))) continue;
			uint32_t type = drm_property_id(fd, plane->plane_id, DRM_MODE_OBJECT_PLANE, "type");
			std::shared_ptr<drmModeObjectProperties> props(
				drmModeObjectGetProperties(fd, plane->plane_id, DRM_MODE_OBJECT_PLANE), drmModeFreeObjectProperties);
			if (!type || !props) continue;
			bool is_overlay = false;
			for (uint32_t p = 0; p < props->count_props; ++p) {
				if (props->props[p] == type && props->prop_values[p] == DRM_PLANE_TYPE_OVERLAY) {
					is_overlay = true;
					break;
				}
			}
			if (!is_overlay) continue;
			for (uint32_t f = 0; f < plane->count_formats; ++f) {
				if (plane->formats[f] == DRM_FORMAT_YUYV) {
					return plane->plane_id;
				}
			}
		}
		return 34; // fallback ID if enumeration doesn't find YUYV
	}

	static int create_dumb_fb(int fd, uint32_t width, uint32_t height, uint32_t drm_format, uint32_t bpp,
	                          uint32_t* out_fb_id, uint32_t* out_bo_handle, uint32_t* out_pitch) {
		struct drm_mode_create_dumb create = {};
		create.width = width;
		create.height = height;
		create.bpp = bpp;
		if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &create) < 0) {
			LOGE("DRM_IOCTL_MODE_CREATE_DUMB(%ux%u, bpp=%u) failed: %d", width, height, bpp, errno);
			return -1;
		}
		uint32_t handles[4] = { create.handle, 0, 0, 0 };
		uint32_t pitches[4] = { create.pitch, 0, 0, 0 };
		uint32_t offsets[4] = { 0, 0, 0, 0 };
		uint32_t fb_id = 0;
		if (drmModeAddFB2(fd, width, height, drm_format, handles, pitches, offsets, &fb_id, 0) < 0) {
			LOGE("drmModeAddFB2(%ux%u, format=0x%x) failed: %d", width, height, drm_format, errno);
			struct drm_mode_destroy_dumb destroy = {};
			destroy.handle = create.handle;
			drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
			return -1;
		}
		*out_fb_id = fb_id;
		*out_bo_handle = create.handle;
		if (out_pitch) *out_pitch = create.pitch;
		return 0;
	}

	static void destroy_dumb_fb(int fd, uint32_t* p_fb_id, uint32_t* p_bo_handle) {
		if (fd < 0) return;
		if (p_fb_id && *p_fb_id) {
			drmModeRmFB(fd, *p_fb_id);
			*p_fb_id = 0;
		}
		if (p_bo_handle && *p_bo_handle) {
			struct drm_mode_destroy_dumb destroy = {};
			destroy.handle = *p_bo_handle;
			drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
			*p_bo_handle = 0;
		}
	}

	static int fill_yuyv_dumb_fb(int fd, uint32_t bo, uint32_t width, uint32_t height, uint32_t pitch) {
		struct drm_mode_map_dumb mreq = {};
		mreq.handle = bo;
		if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq) < 0)
			return -1;
		size_t map_len = (size_t)pitch * height;
		void* map = mmap(nullptr, map_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, mreq.offset);
		if (map == MAP_FAILED)
			return -1;
		for (uint32_t y = 0; y < height; y++) {
			uint8_t* row = (uint8_t*)map + (size_t)y * pitch;
			for (uint32_t x = 0; x + 1 < width; x += 2) {
				row[x * 2 + 0] = 0x10; // Y0: limited-range black
				row[x * 2 + 1] = 0x80; // U
				row[x * 2 + 2] = 0x10; // Y1
				row[x * 2 + 3] = 0x80; // V
			}
		}
		munmap(map, map_len);
		return 0;
	}

	struct ultra_direct_context_t {
		int drm_fd{-1};
		uint32_t connector_id{0};
		uint32_t crtc_id{0};
		int crtc_index{-1};
		uint32_t primary_plane_id{0};
		uint32_t primary_fb_id{0};
		uint32_t primary_bo_handle{0};
		uint32_t overlay_plane_id{0};
		uint32_t overlay_fb_id{0};
		uint32_t overlay_bo_handle{0};
		uint32_t mode_blob_id{0};
		bool modeset_active{false};
		bool overlay_active{false};
		bool avh_active{false};
		drmModeModeInfo current_mode{};

		struct video_format current_video_format{};
		struct video_format last_locked_video_format{};
		/* A fresh DAU service may not receive set_video_format until the
		 * stream changes. Match the RGB format used to prime the plane. */
		uint32_t ultra_overlay_format{DRM_FORMAT_BGR888};
		bool fixed_mode_override{false};
		bool force_dante_source{false};
		uint32_t fixed_width{3840};
		uint32_t fixed_height{2160};
		uint32_t fixed_fps{60};

		bool last_connector_connected{false};
		bool have_last_connector_state{false};
		int uevent_fd{-1};

		struct dau_service* dau{nullptr};
		int dau_fd{-1};
		bool have_last_audio_info{false};
		struct audio_info last_audio_info{};

		uint64_t format_changes{0};
		uint64_t hotplug_events{0};
	};

	static uint32_t g_tpg_pattern = 7; // 7 = XTPG_BKGND_SOLID_BLACK, 9 = XTPG_BKGND_COLOR_BARS

	static std::string find_platform_driver_dir(const char* driver_sub) {
		const char* base_paths[] = {
			"/sys/bus/platform/drivers/zzlab-embeddedsw",
			"/sys/devices/platform/amba_pl",
			"/sys/devices/platform"
		};
		for (const char* base : base_paths) {
			DIR* dir = opendir(base);
			if (!dir) continue;
			struct dirent* ent;
			while ((ent = readdir(dir)) != nullptr) {
				if (ent->d_name[0] == '.') continue;
				if (strstr(ent->d_name, driver_sub)) {
					std::string res = std::string(base) + "/" + ent->d_name;
					closedir(dir);
					return res;
				}
			}
			closedir(dir);
		}
		return "";
	}

	static bool configure_vtpg(uint32_t width, uint32_t height, uint32_t bckgnd_id) {
		std::string dir = find_platform_driver_dir("v-tpg");
		if (dir.empty()) dir = find_platform_driver_dir("tpg");
		if (dir.empty()) {
			LOGW("[ULTRA DIRECT] VTPG sysfs directory not found");
			return false;
		}
		auto write_attr = [&](const char* attr, uint32_t val) {
			std::string file = dir + "/" + attr;
			FILE* f = fopen(file.c_str(), "w");
			if (!f) return false;
			fprintf(f, "%u\n", val);
			fclose(f);
			return true;
		};
		write_attr("width", width);
		write_attr("height", height);
		write_attr("color_format", 0); // 0 = RGB
		write_attr("bckgnd_id", bckgnd_id); // 7 = solid black, 9 = color bars
		write_attr("ovrlay_id", 0);
		write_attr("mask_id", 0);
		write_attr("enable_input", 0);
		write_attr("auto_restart", 1);
		write_attr("start", 1);
		LOGI("[ULTRA DIRECT] Configured VTPG at %s: %ux%u, bckgnd_id=%u", dir.c_str(), width, height, bckgnd_id);
		return true;
	}

	static bool set_vmix_layer1_opaque() {
		std::string dir = find_platform_driver_dir("v-mix");
		if (dir.empty()) dir = find_platform_driver_dir("mix");
		if (dir.empty()) return false;
		auto write_attr = [&](const char* attr, uint32_t val) {
			std::string file = dir + "/" + attr;
			FILE* f = fopen(file.c_str(), "w");
			if (!f) return false;
			fprintf(f, "%u\n", val);
			fclose(f);
			return true;
		};
		write_attr("selected_layer", 1);
		write_attr("layer_alpha", 255);
		return true;
	}

	static int apply_drm_modeset(ultra_direct_context_t* ctx, uint32_t width, uint32_t height,
	                             double fps, bool enable_overlay, uint32_t overlay_format = DRM_FORMAT_YUYV) {
		if (!ctx || ctx->drm_fd < 0 || ctx->connector_id == 0 || ctx->crtc_id == 0)
			return -1;
		if (!width || !height || width > 3840 || height > 2160)
			return -1;

		std::shared_ptr<drmModeConnector> connector(
			drmModeGetConnector(ctx->drm_fd, ctx->connector_id), drmModeFreeConnector);
		if (!connector || connector->connection != DRM_MODE_CONNECTED) return -1;

		drmModeModeInfo mode = {};
		bool exact = find_matching_mode(connector.get(), width, height, fps, false, &mode);
		if (exact && (mode.clock > 594000 || mode_vrefresh(&mode) > 60.5))
			exact = false;
		if (!exact) {
			bool found = false;
			for (int i = 0; i < connector->count_modes; ++i) {
				const drmModeModeInfo& candidate = connector->modes[i];
				if (!candidate.hdisplay || !candidate.vdisplay ||
				    candidate.hdisplay > 3840 || candidate.vdisplay > 2160 ||
				    candidate.clock > 594000 || mode_vrefresh(&candidate) <= 0 ||
				    mode_vrefresh(&candidate) > 60.5 ||
				    (candidate.flags & DRM_MODE_FLAG_INTERLACE)) continue;
				uint64_t area = (uint64_t)candidate.hdisplay * candidate.vdisplay;
				uint64_t best_area = (uint64_t)mode.hdisplay * mode.vdisplay;
				if (!found || area > best_area ||
				    (area == best_area && mode_vrefresh(&candidate) > mode_vrefresh(&mode))) {
					mode = candidate;
					found = true;
				}
			}
			if (!found) {
				LOGE("[ULTRA DIRECT] no VProcSS-compatible EDID mode for %ux%u@%.2f",
				     width, height, fps);
				return -1;
			}
			LOGW("[ULTRA DIRECT] input %ux%u@%.2f not in EDID; scaling to %s (%ux%u@%.2f)",
			     width, height, fps, mode.name, mode.hdisplay, mode.vdisplay,
			     mode_vrefresh(&mode));
		}
		if (fps > mode_vrefresh(&mode) + 0.5)
			LOGW("[ULTRA DIRECT] HDMI %.2f Hz is slower than Ultra input %.2f fps; frames may be dropped",
			     mode_vrefresh(&mode), fps);
		const uint32_t output_w = mode.hdisplay, output_h = mode.vdisplay;
		uint32_t rect_w = output_w, rect_h = output_h;
		if ((uint64_t)width * output_h > (uint64_t)height * output_w)
			rect_h = (uint64_t)output_w * height / width;
		else
			rect_w = (uint64_t)output_h * width / height;
		rect_w &= ~1u;
		rect_h &= ~1u;
		if (!rect_w || !rect_h) return -1;

		uint32_t new_mode_blob_id = 0;
		if (drmModeCreatePropertyBlob(ctx->drm_fd, &mode, sizeof(mode), &new_mode_blob_id) != 0) {
			LOGE("[ULTRA DIRECT] drmModeCreatePropertyBlob failed: %d", errno);
			return -1;
		}

		uint32_t new_primary_fb = 0;
		uint32_t new_primary_bo = 0;
		if (create_dumb_fb(ctx->drm_fd, output_w, output_h, DRM_FORMAT_BGR888, 24,
		                   &new_primary_fb, &new_primary_bo, nullptr) != 0) {
			drmModeDestroyPropertyBlob(ctx->drm_fd, new_mode_blob_id);
			return -1;
		}

		uint32_t new_overlay_fb = 0;
		uint32_t new_overlay_bo = 0;
		uint32_t overlay_pitch = 0;
		if (enable_overlay && ctx->overlay_plane_id != 0) {
			uint32_t bpp = (overlay_format == DRM_FORMAT_YUYV) ? 16 : 24;
			if (create_dumb_fb(ctx->drm_fd, width, height, overlay_format, bpp,
			                   &new_overlay_fb, &new_overlay_bo, &overlay_pitch) == 0) {
				if (overlay_format == DRM_FORMAT_YUYV) {
					fill_yuyv_dumb_fb(ctx->drm_fd, new_overlay_bo, width, height, overlay_pitch);
				}
			} else {
				LOGW("[ULTRA DIRECT] failed to create overlay dumb FB, proceeding without overlay");
				enable_overlay = false;
			}
		}

		std::shared_ptr<drmModeAtomicReq> req(drmModeAtomicAlloc(), drmModeAtomicFree);
		if (!req) {
			destroy_dumb_fb(ctx->drm_fd, &new_primary_fb, &new_primary_bo);
			destroy_dumb_fb(ctx->drm_fd, &new_overlay_fb, &new_overlay_bo);
			drmModeDestroyPropertyBlob(ctx->drm_fd, new_mode_blob_id);
			return -1;
		}

		uint32_t connector_crtc = drm_property_id(ctx->drm_fd, ctx->connector_id, DRM_MODE_OBJECT_CONNECTOR, "CRTC_ID");
		uint32_t crtc_mode = drm_property_id(ctx->drm_fd, ctx->crtc_id, DRM_MODE_OBJECT_CRTC, "MODE_ID");
		uint32_t crtc_active = drm_property_id(ctx->drm_fd, ctx->crtc_id, DRM_MODE_OBJECT_CRTC, "ACTIVE");

		uint32_t p_crtc = drm_property_id(ctx->drm_fd, ctx->primary_plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_ID");
		uint32_t p_fb = drm_property_id(ctx->drm_fd, ctx->primary_plane_id, DRM_MODE_OBJECT_PLANE, "FB_ID");
		uint32_t p_cx = drm_property_id(ctx->drm_fd, ctx->primary_plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_X");
		uint32_t p_cy = drm_property_id(ctx->drm_fd, ctx->primary_plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_Y");
		uint32_t p_cw = drm_property_id(ctx->drm_fd, ctx->primary_plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_W");
		uint32_t p_ch = drm_property_id(ctx->drm_fd, ctx->primary_plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_H");
		uint32_t p_sx = drm_property_id(ctx->drm_fd, ctx->primary_plane_id, DRM_MODE_OBJECT_PLANE, "SRC_X");
		uint32_t p_sy = drm_property_id(ctx->drm_fd, ctx->primary_plane_id, DRM_MODE_OBJECT_PLANE, "SRC_Y");
		uint32_t p_sw = drm_property_id(ctx->drm_fd, ctx->primary_plane_id, DRM_MODE_OBJECT_PLANE, "SRC_W");
		uint32_t p_sh = drm_property_id(ctx->drm_fd, ctx->primary_plane_id, DRM_MODE_OBJECT_PLANE, "SRC_H");
		uint32_t p_alpha = drm_property_id(ctx->drm_fd, ctx->primary_plane_id, DRM_MODE_OBJECT_PLANE, "alpha");

		drmModeAtomicAddProperty(req.get(), ctx->connector_id, connector_crtc, ctx->crtc_id);
		drmModeAtomicAddProperty(req.get(), ctx->crtc_id, crtc_mode, new_mode_blob_id);
		drmModeAtomicAddProperty(req.get(), ctx->crtc_id, crtc_active, 1);

		drmModeAtomicAddProperty(req.get(), ctx->primary_plane_id, p_crtc, ctx->crtc_id);
		drmModeAtomicAddProperty(req.get(), ctx->primary_plane_id, p_fb, new_primary_fb);
		drmModeAtomicAddProperty(req.get(), ctx->primary_plane_id, p_cx, 0);
		drmModeAtomicAddProperty(req.get(), ctx->primary_plane_id, p_cy, 0);
		drmModeAtomicAddProperty(req.get(), ctx->primary_plane_id, p_cw, output_w);
		drmModeAtomicAddProperty(req.get(), ctx->primary_plane_id, p_ch, output_h);
		drmModeAtomicAddProperty(req.get(), ctx->primary_plane_id, p_sx, 0);
		drmModeAtomicAddProperty(req.get(), ctx->primary_plane_id, p_sy, 0);
		drmModeAtomicAddProperty(req.get(), ctx->primary_plane_id, p_sw, output_w << 16);
		drmModeAtomicAddProperty(req.get(), ctx->primary_plane_id, p_sh, output_h << 16);
		if (p_alpha) drmModeAtomicAddProperty(req.get(), ctx->primary_plane_id, p_alpha, 0xffff);

		if (ctx->overlay_plane_id != 0) {
			uint32_t o_source = drm_property_id(ctx->drm_fd, ctx->overlay_plane_id,
				DRM_MODE_OBJECT_PLANE, "SC6F0_OVERLAY_SOURCE");
			if (!o_source) {
				LOGE("[ULTRA DIRECT] shared overlay source property is unavailable");
				destroy_dumb_fb(ctx->drm_fd, &new_primary_fb, &new_primary_bo);
				destroy_dumb_fb(ctx->drm_fd, &new_overlay_fb, &new_overlay_bo);
				drmModeDestroyPropertyBlob(ctx->drm_fd, new_mode_blob_id);
				return -1;
			}
			/* The disabled route is DUMMY; only a locked Dante stream may
			 * select SI0 through the shared VProcSS overlay path. */
			drmModeAtomicAddProperty(req.get(), ctx->overlay_plane_id, o_source,
				enable_overlay ? 1 : 0);
			uint32_t o_crtc = drm_property_id(ctx->drm_fd, ctx->overlay_plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_ID");
			uint32_t o_fb = drm_property_id(ctx->drm_fd, ctx->overlay_plane_id, DRM_MODE_OBJECT_PLANE, "FB_ID");
			uint32_t o_cx = drm_property_id(ctx->drm_fd, ctx->overlay_plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_X");
			uint32_t o_cy = drm_property_id(ctx->drm_fd, ctx->overlay_plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_Y");
			uint32_t o_cw = drm_property_id(ctx->drm_fd, ctx->overlay_plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_W");
			uint32_t o_ch = drm_property_id(ctx->drm_fd, ctx->overlay_plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_H");
			uint32_t o_sx = drm_property_id(ctx->drm_fd, ctx->overlay_plane_id, DRM_MODE_OBJECT_PLANE, "SRC_X");
			uint32_t o_sy = drm_property_id(ctx->drm_fd, ctx->overlay_plane_id, DRM_MODE_OBJECT_PLANE, "SRC_Y");
			uint32_t o_sw = drm_property_id(ctx->drm_fd, ctx->overlay_plane_id, DRM_MODE_OBJECT_PLANE, "SRC_W");
			uint32_t o_sh = drm_property_id(ctx->drm_fd, ctx->overlay_plane_id, DRM_MODE_OBJECT_PLANE, "SRC_H");
			uint32_t o_alpha = drm_property_id(ctx->drm_fd, ctx->overlay_plane_id, DRM_MODE_OBJECT_PLANE, "alpha");

			if (enable_overlay && new_overlay_fb != 0) {
				drmModeAtomicAddProperty(req.get(), ctx->overlay_plane_id, o_crtc, ctx->crtc_id);
				drmModeAtomicAddProperty(req.get(), ctx->overlay_plane_id, o_fb, new_overlay_fb);
				drmModeAtomicAddProperty(req.get(), ctx->overlay_plane_id, o_cx, (output_w - rect_w) / 2);
				drmModeAtomicAddProperty(req.get(), ctx->overlay_plane_id, o_cy, (output_h - rect_h) / 2);
				drmModeAtomicAddProperty(req.get(), ctx->overlay_plane_id, o_cw, rect_w);
				drmModeAtomicAddProperty(req.get(), ctx->overlay_plane_id, o_ch, rect_h);
				drmModeAtomicAddProperty(req.get(), ctx->overlay_plane_id, o_sx, 0);
				drmModeAtomicAddProperty(req.get(), ctx->overlay_plane_id, o_sy, 0);
				drmModeAtomicAddProperty(req.get(), ctx->overlay_plane_id, o_sw, width << 16);
				drmModeAtomicAddProperty(req.get(), ctx->overlay_plane_id, o_sh, height << 16);
				if (o_alpha) drmModeAtomicAddProperty(req.get(), ctx->overlay_plane_id, o_alpha, 0xffff);
			} else {
				drmModeAtomicAddProperty(req.get(), ctx->overlay_plane_id, o_crtc, 0);
				drmModeAtomicAddProperty(req.get(), ctx->overlay_plane_id, o_fb, 0);
			}
		}

		int err = drmModeAtomicCommit(ctx->drm_fd, req.get(), DRM_MODE_ATOMIC_ALLOW_MODESET, nullptr);
		if (err != 0) {
			LOGE("[ULTRA DIRECT] drmModeAtomicCommit failed: %d", errno);
			destroy_dumb_fb(ctx->drm_fd, &new_primary_fb, &new_primary_bo);
			destroy_dumb_fb(ctx->drm_fd, &new_overlay_fb, &new_overlay_bo);
			drmModeDestroyPropertyBlob(ctx->drm_fd, new_mode_blob_id);
			return -1;
		}

		destroy_dumb_fb(ctx->drm_fd, &ctx->primary_fb_id, &ctx->primary_bo_handle);
		destroy_dumb_fb(ctx->drm_fd, &ctx->overlay_fb_id, &ctx->overlay_bo_handle);
		if (ctx->mode_blob_id)
			drmModeDestroyPropertyBlob(ctx->drm_fd, ctx->mode_blob_id);

		ctx->primary_fb_id = new_primary_fb;
		ctx->primary_bo_handle = new_primary_bo;
		ctx->overlay_fb_id = new_overlay_fb;
		ctx->overlay_bo_handle = new_overlay_bo;
		ctx->mode_blob_id = new_mode_blob_id;
		ctx->modeset_active = true;
		ctx->overlay_active = enable_overlay;
		ctx->current_mode = mode;

		// Ensure VTPG is generating frames to keep v_mix master layer active
		configure_vtpg(output_w, output_h, g_tpg_pattern);
		if (enable_overlay) {
			set_vmix_layer1_opaque();
		}

		LOGI("[ULTRA DIRECT] DRM modeset active: HDMI %ux%u@%.2fHz (%s) | source %ux%u@%.2f | primary plane %u, overlay plane %u (active=%d)",
		     mode.hdisplay, mode.vdisplay, mode_vrefresh(&mode), mode.name,
		     width, height, fps,
		     ctx->primary_plane_id, ctx->overlay_plane_id, (int)enable_overlay);
		return 0;
	}

	static void cleanup_ultra_direct_drm(ultra_direct_context_t* ctx) {
		if (!ctx || ctx->drm_fd < 0) return;
		destroy_dumb_fb(ctx->drm_fd, &ctx->primary_fb_id, &ctx->primary_bo_handle);
		destroy_dumb_fb(ctx->drm_fd, &ctx->overlay_fb_id, &ctx->overlay_bo_handle);
		if (ctx->mode_blob_id) {
			drmModeDestroyPropertyBlob(ctx->drm_fd, ctx->mode_blob_id);
			ctx->mode_blob_id = 0;
		}
		drmClose(ctx->drm_fd);
		ctx->drm_fd = -1;
		ctx->modeset_active = false;
	}

	static const char* dau_colorformat_str(enum video_colorformat fmt) {
		switch (fmt) {
		case VIDEO_COLORFORMAT_YUV444: return "YUV444";
		case VIDEO_COLORFORMAT_YUV422: return "YUV422";
		case VIDEO_COLORFORMAT_YUV420: return "YUV420";
		case VIDEO_COLORFORMAT_RGB:    return "RGB";
		default: return "unknown";
		}
	}

	static void dau_get_cable_status(struct dau_service *dau, struct DBusMessage *reply, void *ctxt) {
		auto* ctx = static_cast<ultra_direct_context_t*>(ctxt);
		bool connected = false;
		if (ctx && ctx->drm_fd >= 0 && ctx->connector_id != 0) {
			std::shared_ptr<drmModeConnector> conn(
				drmModeGetConnector(ctx->drm_fd, ctx->connector_id), drmModeFreeConnector);
			if (conn) connected = (conn->connection == DRM_MODE_CONNECTED);
		}
		LOGI("[ULTRA DIRECT][DAU] get_cable_status -> %s", connected ? "connected" : "disconnected");
		dau_reply_get_cable_status(dau, reply, connected);
	}

	static void dau_get_hdcp(struct dau_service *dau, struct DBusMessage *reply, void *ctxt) {
		(void)ctxt;
		struct hdcp h = {};
		h.current = HDCP_VERSION_22;
		h.hdcp14_supported = false;
		h.hdcp22_supported = true;
		LOGI("[ULTRA DIRECT][DAU] get_hdcp -> HDCP 2.2");
		dau_reply_get_hdcp(dau, reply, &h);
	}

	static void dau_set_hdcp(struct dau_service *dau, struct DBusMessage *reply,
	                         enum hdcp_version version, void *ctxt) {
		(void)ctxt;
		LOGI("[ULTRA DIRECT][DAU] set_hdcp (version=%d)", (int)version);
		dau_reply_success(dau, reply);
	}

	static void dau_get_edid(struct dau_service *dau, struct DBusMessage *reply, void *ctxt) {
		auto* ctx = static_cast<ultra_direct_context_t*>(ctxt);
		unsigned char edid[EDID_LEN];
		memset(edid, 0, sizeof(edid));
		bool valid = false;
		if (ctx && ctx->drm_fd >= 0 && ctx->connector_id != 0) {
			valid = read_connector_edid(ctx->drm_fd, ctx->connector_id, edid, sizeof(edid));
		}
		if (valid) {
			LOGI("[ULTRA DIRECT][DAU] get_edid -> returning display EDID");
			dau_reply_get_edid(dau, reply, edid);
		} else {
			LOGI("[ULTRA DIRECT][DAU] get_edid -> display EDID unavailable, returning fallback EDID");
			dau_reply_get_edid(dau, reply, g_fallback_edid);
		}
	}

	static void dau_set_video_format(struct dau_service *dau, struct DBusMessage *reply,
	                                 const struct video_format *format, void *ctxt) {
		auto* ctx = static_cast<ultra_direct_context_t*>(ctxt);
		if (!format || !ctx) {
			LOGW("[ULTRA DIRECT][DAU] set_video_format: invalid parameter");
			dau_reply_error(dau, reply, "NULL format");
			return;
		}
		LOGI("[ULTRA DIRECT][DAU] set_video_format: %ux%u@%uHz, color=%s, bpp=%u, interlaced=%s, locked=%s",
		     format->width, format->height, format->framerate,
		     dau_colorformat_str(format->colorformat), format->bpp,
		     format->interlaced ? "true" : "false",
		     format->locked ? "true" : "false");

		ctx->current_video_format = *format;
		ctx->format_changes++;
		if (format->locked && format->width && format->height && format->framerate) {
			ctx->last_locked_video_format = *format;
			ctx->ultra_overlay_format = format->colorformat == VIDEO_COLORFORMAT_RGB ?
				DRM_FORMAT_BGR888 : DRM_FORMAT_YUYV;
		}
		if (ctx->avh_active) {
			/* AVH owns the shared VProcSS and HDMI timing for now. The last
			 * Ultra format is retained for the next return to DANTE. */
			dau_reply_success(dau, reply);
			return;
		}
		if (ctx->force_dante_source && !format->locked) {
			LOGI("[ULTRA DIRECT][DAU] stream unlocked; explicit DANTE source remains selected");
			dau_reply_success(dau, reply);
			return;
		}

		uint32_t target_w = ctx->fixed_mode_override ? ctx->fixed_width : format->width;
		uint32_t target_h = ctx->fixed_mode_override ? ctx->fixed_height : format->height;
		double target_fps = ctx->fixed_mode_override ? (double)ctx->fixed_fps : (double)format->framerate;
		uint32_t overlay_format = ctx->ultra_overlay_format;

		int result;
		if (format->locked) {
			result = apply_drm_modeset(ctx, target_w, target_h, target_fps, true, overlay_format);
		} else {
			LOGI("[ULTRA DIRECT][DAU] stream unlocked: disabling overlay plane");
			result = apply_drm_modeset(ctx, target_w, target_h, target_fps, false);
		}
		if (result == 0)
			dau_reply_success(dau, reply);
		else {
			/* Do not continue claiming the previous HDMI picture as this
			 * newly reported Ultra format. Keep the mixer alive on DUMMY. */
			if (format->locked && ctx->current_mode.hdisplay)
				apply_drm_modeset(ctx, ctx->current_mode.hdisplay,
					ctx->current_mode.vdisplay, mode_vrefresh(&ctx->current_mode), false);
			dau_reply_error(dau, reply, "DRM overlay modeset failed");
		}
	}

	static int restore_ultra_output(ultra_direct_context_t* ctx) {
		const struct video_format& format = ctx->last_locked_video_format;
		uint32_t width = ctx->fixed_mode_override ? ctx->fixed_width :
			(format.locked ? format.width : ctx->fixed_width);
		uint32_t height = ctx->fixed_mode_override ? ctx->fixed_height :
			(format.locked ? format.height : ctx->fixed_height);
		uint32_t fps = ctx->fixed_mode_override ? ctx->fixed_fps :
			(format.locked ? format.framerate : ctx->fixed_fps);
		LOGI("[AUTO] restoring Ultra input %ux%u@%u (%s)%s", width, height, fps,
		     ctx->ultra_overlay_format == DRM_FORMAT_BGR888 ? "RGB" : "YUYV",
		     format.locked ? "" : " (DAU format not yet received; startup fallback)");
		return apply_drm_modeset(ctx, width, height, fps, true, ctx->ultra_overlay_format);
	}

	static void dau_set_audio_info(struct dau_service *dau, struct DBusMessage *reply,
	                               const struct audio_info *info, void *ctxt) {
		ultra_direct_context_t* ctx = (ultra_direct_context_t*)ctxt;
		if (ctx && info) {
			const struct audio_info* previous = &ctx->last_audio_info;
			bool changed = !ctx->have_last_audio_info ||
				info->channel_count != previous->channel_count ||
				info->speaker_map != previous->speaker_map ||
				info->level_shift_value != previous->level_shift_value ||
				info->downmix_inhibit != previous->downmix_inhibit ||
				info->lfe_playback_level != previous->lfe_playback_level ||
				info->sample_rate != previous->sample_rate ||
				info->encoding != previous->encoding ||
				info->sample_bits != previous->sample_bits;
			if (changed) {
				ctx->last_audio_info = *info;
				ctx->have_last_audio_info = true;
				LOGI("[ULTRA DIRECT][DAU] set_audio_info changed: channels=%u speaker_map=%u level_shift=%u downmix_inhibit=%d lfe_level=%u rate=%u bits=%u encoding=%u",
				     info->channel_count, (unsigned int)info->speaker_map,
				     info->level_shift_value, (int)info->downmix_inhibit,
				     info->lfe_playback_level, info->sample_rate,
				     info->sample_bits, (unsigned int)info->encoding);
			}
		}
		dau_reply_success(dau, reply);
	}

	static void dau_set_hdr(struct dau_service *dau, struct DBusMessage *reply,
	                        const struct hdr_info *info, void *ctxt) {
		(void)ctxt;
		if (info) {
			LOGI("[ULTRA DIRECT][DAU] set_hdr: avi_c=%u, avi_ec=%u, avi_q=%u, dv_tunneling=%s",
			     (unsigned int)info->avi_c, (unsigned int)info->avi_ec, (unsigned int)info->avi_q,
			     info->dv_tunneling ? "true" : "false");
		}
		dau_reply_success(dau, reply);
	}

	static void dau_toggle_hpd(struct dau_service *dau, struct DBusMessage *reply, void *ctxt) {
		(void)ctxt;
		LOGI("[ULTRA DIRECT][DAU] toggle_hpd");
		dau_reply_success(dau, reply);
	}

	static const struct dau_service_methods g_ultra_direct_dau_methods = {
		.get_cable_status = dau_get_cable_status,
		.get_hdcp = dau_get_hdcp,
		.set_hdcp = dau_set_hdcp,

		.get_video_format = nullptr,
		.get_audio_info = nullptr,
		.get_hdr = nullptr,
		.set_edid = nullptr,
		.toggle_hpd = dau_toggle_hpd,

		.set_video_format = dau_set_video_format,
		.set_audio_info = dau_set_audio_info,
		.set_hdr = dau_set_hdr,
		.get_edid = dau_get_edid,
	};

	static int run_ultra_direct(uint32_t stat_interval, bool fixed_mode_override,
	                            bool force_dante_source,
	                            uint32_t fixed_w, uint32_t fixed_h, uint32_t fixed_fps,
	                            bool auto_mode = false,
	                            const std::string& dvr_socket = "/var/run/dante/dvr",
	                            const std::string& dep_name = "DanteEP",
	                            const std::string& channels = "0,1",
	                            uint32_t sample_rate = 48000,
	                            uint32_t reorder_window = 1,
	                            uint32_t reorder_timeout_ms = 1) {
		if (stat_interval == 0)
			stat_interval = 3;
		LOGI("[ULTRA DIRECT] Dante Ultra FPGA media path: VSINK video (v_mix layer 1) and AES3 audio (SI1) -> HDMI");
		LOGI("[ULTRA DIRECT] Hardware passthrough: bypasses DVR socket, DEP, VCU, and ALSA PCM playback.");

		if (select_hdmi_audio_route(1) != 0) {
			LOGE("[ULTRA DIRECT] failed to select SI1 (Custom FPGA / Dante RX) audio route");
			return 1;
		}
		int initial_route = read_hdmi_audio_route();
		LOGI("[ULTRA DIRECT] HDMI audio route set to SI1 (%s)",
		     hdmi_audio_route_name(initial_route));

		ultra_direct_context_t ctx;
		ctx.fixed_mode_override = fixed_mode_override;
		ctx.force_dante_source = force_dante_source;
		ctx.fixed_width = fixed_w;
		ctx.fixed_height = fixed_h;
		ctx.fixed_fps = fixed_fps;

		ctx.drm_fd = drmOpen("xlnx", nullptr);
		if (ctx.drm_fd < 0 ||
		    drmSetClientCap(ctx.drm_fd, DRM_CLIENT_CAP_ATOMIC, 1) ||
		    drmSetClientCap(ctx.drm_fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1)) {
			LOGE("[ULTRA DIRECT] failed to open DRM xlnx device or enable universal planes: %d", errno);
			return 1;
		}

		std::shared_ptr<drmModeRes> resources(drmModeGetResources(ctx.drm_fd), drmModeFreeResources);
		if (!resources) {
			LOGE("[ULTRA DIRECT] drmModeGetResources failed");
			cleanup_ultra_direct_drm(&ctx);
			return 1;
		}

		for (int i = 0; i < resources->count_connectors; i++) {
			std::shared_ptr<drmModeConnector> conn(
				drmModeGetConnector(ctx.drm_fd, resources->connectors[i]), drmModeFreeConnector);
			if (!conn) continue;
			if (conn->connector_type == DRM_MODE_CONNECTOR_HDMIA) {
				ctx.connector_id = conn->connector_id;
				ctx.last_connector_connected = (conn->connection == DRM_MODE_CONNECTED);
				ctx.have_last_connector_state = true;
				break;
			}
		}
		if (ctx.connector_id == 0) {
			LOGE("[ULTRA DIRECT] HDMI-A connector not found");
			cleanup_ultra_direct_drm(&ctx);
			return 1;
		}

		std::shared_ptr<drmModeConnector> connector(
			drmModeGetConnector(ctx.drm_fd, ctx.connector_id), drmModeFreeConnector);
		if (!find_connector_crtc(ctx.drm_fd, resources.get(), connector.get(),
		                         &ctx.crtc_id, &ctx.crtc_index)) {
			LOGE("[ULTRA DIRECT] no usable CRTC found for HDMI connector %u", ctx.connector_id);
			cleanup_ultra_direct_drm(&ctx);
			return 1;
		}

		ctx.primary_plane_id = find_plane_by_type(ctx.drm_fd, ctx.crtc_index, DRM_PLANE_TYPE_PRIMARY);
		ctx.overlay_plane_id = find_overlay_plane_yuyv(ctx.drm_fd, ctx.crtc_index);
		if (!ctx.primary_plane_id || !ctx.overlay_plane_id ||
		    !drm_property_id(ctx.drm_fd, ctx.overlay_plane_id,
			DRM_MODE_OBJECT_PLANE, "SC6F0_OVERLAY_SOURCE")) {
			LOGE("[ULTRA DIRECT] shared Dante overlay plane is unavailable");
			cleanup_ultra_direct_drm(&ctx);
			return 1;
		}
		LOGI("[ULTRA DIRECT] DRM setup: connector=%u (connected=%d), CRTC=%u (idx=%d), primary plane=%u, overlay plane=%u",
		     ctx.connector_id, (int)ctx.last_connector_connected, ctx.crtc_id, ctx.crtc_index,
		     ctx.primary_plane_id, ctx.overlay_plane_id);

		uint32_t init_w = fixed_mode_override ? fixed_w : 3840;
		uint32_t init_h = fixed_mode_override ? fixed_h : 2160;
		double init_fps = fixed_mode_override ? (double)fixed_fps : 60.0;
		if (auto_mode && !fixed_mode_override) {
			/* Until DAU reports Ultra's output format, start with a mode
			 * advertised by this monitor, never an assumed 4K input that
			 * would require a second scaler on the Ultra route. */
			const drmModeModeInfo* best = nullptr;
			for (int i = 0; connector && i < connector->count_modes; ++i) {
				const drmModeModeInfo* candidate = &connector->modes[i];
				if (!candidate->hdisplay || !candidate->vdisplay ||
				    candidate->hdisplay > 3840 || candidate->vdisplay > 2160 ||
				    candidate->clock > 594000 || mode_vrefresh(candidate) <= 0 ||
				    mode_vrefresh(candidate) > 60.5 ||
				    (candidate->flags & DRM_MODE_FLAG_INTERLACE)) continue;
				uint64_t area = (uint64_t)candidate->hdisplay * candidate->vdisplay;
				uint64_t best_area = best ? (uint64_t)best->hdisplay * best->vdisplay : 0;
				if (!best || area > best_area ||
				    (area == best_area &&
				     (mode_vrefresh(candidate) > mode_vrefresh(best) + 0.005 ||
				      (candidate->clock == 594000 && best->clock != 594000))))
					best = candidate;
			}
			if (!best) {
				LOGE("[AUTO] monitor has no supported EDID mode for Ultra startup");
				cleanup_ultra_direct_drm(&ctx);
				return 1;
			}
			init_w = best->hdisplay;
			init_h = best->vdisplay;
			init_fps = mode_vrefresh(best);
			ctx.fixed_width = init_w;
			ctx.fixed_height = init_h;
			ctx.fixed_fps = (uint32_t)(init_fps + 0.5);
			LOGI("[AUTO] Ultra startup EDID mode %ux%u@%.2f until DAU reports its output",
			     init_w, init_h, init_fps);
		}
		/* Explicit source selection does not wait for a DAU format event.
		 * The fixed mode and RGB input format prime the shared VProcSS and
		 * route FPGA Dante video; a later locked event can refine the format. */
		if (apply_drm_modeset(&ctx, init_w, init_h, init_fps,
				force_dante_source, DRM_FORMAT_BGR888) != 0) {
			LOGW("[ULTRA DIRECT] initial DRM modeset failed, will retry on format update or hotplug");
			if (force_dante_source) {
				cleanup_ultra_direct_drm(&ctx);
				return 1;
			}
		}

		ctx.uevent_fd = open_drm_uevent_socket();
		if (ctx.uevent_fd < 0) {
			LOGW("[ULTRA DIRECT] open_drm_uevent_socket failed: %d (hotplug detection degraded)", errno);
		} else {
			LOGI("[ULTRA DIRECT] DRM connector monitor active (uevent fd=%d)", ctx.uevent_fd);
		}

		ctx.dau = dau_service_register(DAU_SERVICE_SINK, 0, &g_ultra_direct_dau_methods, &ctx);
		if (!ctx.dau) {
			LOGE("[ULTRA DIRECT] dau_service_register(DAU_SERVICE_SINK) failed");
			cleanup_ultra_direct_drm(&ctx);
			if (ctx.uevent_fd >= 0) close(ctx.uevent_fd);
			return 1;
		}
		ctx.dau_fd = dau_service_get_fd(ctx.dau);
		LOGI("[ULTRA DIRECT] DAU SINK service registered (fd=%d)", ctx.dau_fd);
		if (ctx.last_connector_connected) {
			dau_signal_cable_change(ctx.dau);
		}
		/* Keep the DVR client present even while Ultra is selected. It is the
		 * sole AVH client, so Controller routing survives source changes and
		 * FLOW_CREATED/DELETED are observed without restarting the session. */
		zst_dante_rx_t* avh = nullptr;
		if (auto_mode) {
			avh = start_dante_rx(dvr_socket, dep_name, channels, sample_rate,
				stat_interval, "", true, false, false, "", "0.0.0.0", 16490,
				false, reorder_window, reorder_timeout_ms, true);
			if (!avh) {
				LOGE("[AUTO] could not start AVH flow monitor/decoder");
				dau_service_unregister(ctx.dau);
				if (ctx.uevent_fd >= 0) close(ctx.uevent_fd);
				cleanup_ultra_direct_drm(&ctx);
				return 1;
			}
			LOGI("[AUTO] default source DANTE; waiting for AVH flow and first decoded frame");
		}
		uint64_t avh_decoded_at_flow = 0;
		bool avh_pending = false;
		bool avh_retry_blocked = false;

		int stdin_flags = fcntl(STDIN_FILENO, F_GETFL, 0);
		if (stdin_flags >= 0)
			fcntl(STDIN_FILENO, F_SETFL, stdin_flags | O_NONBLOCK);

		uint64_t last_report_ms = 0;
		bool have_last_status = false;
		int last_status_route = 0;
		uint32_t last_status_width = 0, last_status_height = 0, last_status_refresh = 0;
		std::string last_status_mode_name;
		bool last_status_overlay_active = false;
		uint64_t last_status_format_changes = 0, last_status_hotplug_events = 0;
		struct pollfd pfds[3];

		while (!g_stop_requested) {
			if (avh) {
				bool flow = avh->auto_flow_count.load(std::memory_order_acquire) != 0;
				if (flow && !avh_pending && !ctx.avh_active && !avh_retry_blocked) {
					avh_pending = true;
					avh_decoded_at_flow = avh->decoded_frames.load();
					LOGI("[AUTO] AVH flow present; waiting for first decoded frame");
				}
				if (!flow) {
					avh_pending = false;
					avh_retry_blocked = false;
				}
				if (flow && avh_pending && !ctx.avh_active &&
				    avh->decoded_frames.load() > avh_decoded_at_flow &&
				    !avh->decoder_initializing.load()) {
					uint32_t w = ctx.current_mode.hdisplay ? ctx.current_mode.hdisplay : fixed_w;
					uint32_t h = ctx.current_mode.vdisplay ? ctx.current_mode.vdisplay : fixed_h;
					LOGI("[AUTO] AVH frame ready; DANTE -> DUMMY -> VCU");
					if (apply_drm_modeset(&ctx, w, h, fixed_fps, false) == 0 &&
					    start_drm_output(avh) == 0) {
						std::lock_guard<std::mutex> guard(avh->display_lock);
						if (avh->auto_flow_count.load() != 0 && start_vdec_sink(avh) == 0) {
							avh->display_enabled = true;
							ctx.avh_active = true;
							LOGI("[AUTO] selected VCU (AVH), input=%ux%u, output=%ux%u",
							     avh->dec_width, avh->dec_height,
							     avh->drm_output_mode.hdisplay, avh->drm_output_mode.vdisplay);
						}
					}
					if (ctx.avh_active && start_hdmi_audio_sink(avh) != 0)
						LOGW("[AUTO] AVH audio unavailable; displaying video");
					if (!ctx.avh_active) {
						stop_drm_output(avh);
						select_hdmi_audio_route(1);
						if (restore_ultra_output(&ctx) != 0)
							LOGE("[AUTO] failed to restore Ultra after AVH initialization error");
						/* Avoid repeated DRM reinitialization on a bad EDID. */
						avh_retry_blocked = true;
						avh_pending = false;
					}
				}
				if (!flow && ctx.avh_active) {
					LOGI("[AUTO] AVH flow removed; VCU -> DUMMY -> DANTE");
					avh->display_enabled = false;
					/* Stop displaying decoded frames before releasing the sink;
					 * route the mixer to the continuous dummy path first. */
					{
						std::lock_guard<std::mutex> guard(avh->display_lock);
						apply_drm_modeset(&ctx, fixed_w, fixed_h, fixed_fps, false);
						if (avh->vdec_sink) {
							if (avh->vdec_sink_started) {
								qcap2_video_sink_stop(avh->vdec_sink);
							}
							qcap2_video_sink_delete(avh->vdec_sink);
							avh->vdec_sink = nullptr;
							avh->vdec_sink_started = false;
						}
					}
					stop_hdmi_audio_sink(avh);
					stop_drm_output(avh);
					select_hdmi_audio_route(1);
					ctx.avh_active = false;
					if (restore_ultra_output(&ctx) != 0)
						LOGE("[AUTO] failed to restore Ultra output; waiting for format/hotplug update");
					else
						LOGI("[AUTO] selected DANTE (Ultra)");
				}
			}
			int nfds = 0;
			int idx_dau = -1, idx_uevent = -1, idx_stdin = -1;

			if (ctx.dau_fd >= 0) {
				idx_dau = nfds;
				pfds[nfds].fd = ctx.dau_fd;
				pfds[nfds].events = POLLIN;
				pfds[nfds].revents = 0;
				nfds++;
			}
			if (ctx.uevent_fd >= 0) {
				idx_uevent = nfds;
				pfds[nfds].fd = ctx.uevent_fd;
				pfds[nfds].events = POLLIN;
				pfds[nfds].revents = 0;
				nfds++;
			}
			idx_stdin = nfds;
			pfds[nfds].fd = STDIN_FILENO;
			pfds[nfds].events = POLLIN;
			pfds[nfds].revents = 0;
			nfds++;

			int ret = poll(pfds, nfds, 500);
			if (ret < 0) {
				if (errno == EINTR) continue;
				LOGE("[ULTRA DIRECT] poll error: %d", errno);
				break;
			}

			if (idx_dau >= 0 && (pfds[idx_dau].revents & POLLIN)) {
				dau_service_dispatch(ctx.dau, 0);
			}

			if (idx_uevent >= 0 && (pfds[idx_uevent].revents & POLLIN)) {
				char uevent_buf[4096];
				bool have_connector_event = false;
				while (true) {
					ssize_t n = recv(ctx.uevent_fd, uevent_buf, sizeof(uevent_buf), 0);
					if (n < 0) break;
					if (is_target_connector_uevent(uevent_buf, n))
						have_connector_event = true;
				}
				if (have_connector_event) {
					std::shared_ptr<drmModeConnector> conn(
						drmModeGetConnector(ctx.drm_fd, ctx.connector_id), drmModeFreeConnector);
					bool connected = conn && (conn->connection == DRM_MODE_CONNECTED);
					if (!ctx.have_last_connector_state || connected != ctx.last_connector_connected) {
						ctx.last_connector_connected = connected;
						ctx.have_last_connector_state = true;
						ctx.hotplug_events++;
						LOGI("[ULTRA DIRECT] HDMI connector state changed -> %s (event)",
						     connected ? "connected" : "disconnected");
						if (ctx.dau)
							dau_signal_cable_change(ctx.dau);
						if (!connected && ctx.overlay_active && !ctx.avh_active) {
							apply_drm_modeset(&ctx, ctx.current_mode.hdisplay, ctx.current_mode.vdisplay,
							                  mode_vrefresh(&ctx.current_mode), false);
						}
					}
				}
			}

			if (idx_stdin >= 0 && (pfds[idx_stdin].revents & POLLIN)) {
				char ch = 0;
				if (read(STDIN_FILENO, &ch, 1) > 0) {
					if (ch == 'q' || ch == 'Q') {
						LOGI("[ULTRA DIRECT] 'q' pressed, stopping...");
						g_stop_requested = 1;
						break;
					}
				}
			}

			struct timespec ts;
			clock_gettime(CLOCK_MONOTONIC, &ts);
			uint64_t now_ms = (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
			if (now_ms - last_report_ms >= (uint64_t)stat_interval * 1000) {
				last_report_ms = now_ms;
				int route = read_hdmi_audio_route();
				std::string mode_name(ctx.current_mode.name);
				bool changed = !have_last_status ||
					route != last_status_route ||
					ctx.current_mode.hdisplay != last_status_width ||
					ctx.current_mode.vdisplay != last_status_height ||
					ctx.current_mode.vrefresh != last_status_refresh ||
					mode_name != last_status_mode_name ||
					ctx.overlay_active != last_status_overlay_active ||
					ctx.format_changes != last_status_format_changes ||
					ctx.hotplug_events != last_status_hotplug_events;
				if (changed) {
					have_last_status = true;
					last_status_route = route;
					last_status_width = ctx.current_mode.hdisplay;
					last_status_height = ctx.current_mode.vdisplay;
					last_status_refresh = ctx.current_mode.vrefresh;
					last_status_mode_name = mode_name;
					last_status_overlay_active = ctx.overlay_active;
					last_status_format_changes = ctx.format_changes;
					last_status_hotplug_events = ctx.hotplug_events;
					LOGI("[ULTRA DIRECT] status: audio route=%d (%s) | display: %ux%u@%uHz (%s) | overlay=%s | fmt changes=%llu | hotplug=%llu",
					     route, hdmi_audio_route_name(route),
					     ctx.current_mode.hdisplay, ctx.current_mode.vdisplay, ctx.current_mode.vrefresh,
					     mode_name.c_str(),
					     ctx.overlay_active ? "active (Dante RX stream visible)" : "inactive (background)",
					     (unsigned long long)ctx.format_changes,
					     (unsigned long long)ctx.hotplug_events);
				}
			}
		}

		if (stdin_flags >= 0)
			fcntl(STDIN_FILENO, F_SETFL, stdin_flags);
		if (avh) {
			avh->display_enabled = false;
			apply_drm_modeset(&ctx, fixed_w, fixed_h, fixed_fps, false);
			stop_dante_rx(avh);
		}

		LOGI("[ULTRA DIRECT] Shutting down, unregistering DAU service...");
		if (ctx.dau) {
			dau_service_unregister(ctx.dau);
			ctx.dau = nullptr;
		}
		if (ctx.uevent_fd >= 0) {
			close(ctx.uevent_fd);
			ctx.uevent_fd = -1;
		}
		LOGI("[ULTRA DIRECT] Releasing DRM resources...");
		cleanup_ultra_direct_drm(&ctx);
		LOGI("[ULTRA DIRECT] Stopped cleanly.");
		return 0;
	}

	/* Standalone RTSP playback bridge. It intentionally owns only its RTSP
	 * elements and the existing HDMI output resources; it does not create a
	 * Dante session/coordinator or enter AVH/Ultra source arbitration. */
	struct rtsp_playback_t {
		zst_element_t *source = nullptr, *vparser = nullptr, *aparser = nullptr, *adec = nullptr;
		zst_element_t *audio_adapter = nullptr, *aresampler = nullptr;
		zst_element_t *vsink = nullptr, *asink = nullptr;
		zst_dante_rx_t *output = nullptr;
		std::atomic<uint64_t> video_frames{0}, audio_frames{0};
		uint32_t audio_rate = 0, audio_channels = 0, audio_format = UINT32_MAX;
	};
	struct rtsp_sink_priv_t { rtsp_playback_t *playback; bool video; };

	static zst_result_t rtsp_video_frame(rtsp_playback_t *p, zst_buffer_t *buf) {
		if (!p || !p->output || !buf || !buf->memory.data) return ZST_ERROR;
		rx_video_stats_t *stats = p->output->video_stats;
		stats->total_packets++;
		stats->total_bytes += buf->memory.size;
		rx_video_feed_decoder(stats, (const uint8_t*)buf->memory.data,
		                      buf->memory.size, buf->flags, buf->pts);
		p->video_frames++;
		return ZST_OK;
	}

	/* AAC decoder uses AVSampleFormat values; audioresample uses ZST_AUDIO_FMT.
	 * Translate metadata only, keeping the decoder buffer alive through the
	 * resampler. Never reinterpret enum values or planar data as packed PCM. */
	static zst_result_t rtsp_audio_adapter_push(zst_pad_t *pad, zst_buffer_t *buf) {
		if (!pad || !pad->parent || !buf || !buf->payload) return ZST_ERROR;
		auto *s = (rtsp_sink_priv_t*)pad->parent->priv;
		auto *p = s->playback;
		auto *af = (zst_audio_frame_t*)buf->payload;
		uint32_t format;
		const char *format_name;
		switch ((enum AVSampleFormat)af->format) {
		case AV_SAMPLE_FMT_U8: format = ZST_AUDIO_FMT_U8; format_name = "u8"; break;
		case AV_SAMPLE_FMT_S16: format = ZST_AUDIO_FMT_S16LE; format_name = "s16"; break;
		case AV_SAMPLE_FMT_S32: format = ZST_AUDIO_FMT_S32LE; format_name = "s32"; break;
		case AV_SAMPLE_FMT_FLT: format = ZST_AUDIO_FMT_F32LE; format_name = "flt"; break;
		case AV_SAMPLE_FMT_S16P: format = ZST_AUDIO_FMT_S16P; format_name = "s16p"; break;
		case AV_SAMPLE_FMT_S32P: format = ZST_AUDIO_FMT_S32P; format_name = "s32p"; break;
		case AV_SAMPLE_FMT_FLTP: format = ZST_AUDIO_FMT_F32P; format_name = "fltp"; break;
		default: LOGE("Unsupported AAC decoder sample format: %u", af->format); return ZST_ERROR;
		}
		if (p->audio_rate != af->sample_rate || p->audio_channels != af->channels || p->audio_format != af->format) {
			LOGI("RTSP AAC output: %u Hz, %u channels, %s -> shared PCM 48000 Hz stereo S32LE",
			     af->sample_rate, af->channels, format_name);
			p->audio_rate = af->sample_rate; p->audio_channels = af->channels; p->audio_format = af->format;
		}
		auto *input = zst_buffer_create(ZST_BUFFER_AUDIO_FRAME);
		auto *frame = (zst_audio_frame_t*)malloc(sizeof(zst_audio_frame_t));
		if (!input || !frame) { if (input) zst_buffer_unref(input); free(frame); return ZST_ERROR; }
		*frame = *af;
		frame->format = format;
		input->payload = frame;
		input->memory.data = buf->memory.data;
		input->memory.size = buf->memory.size;
		input->memory.priv = zst_buffer_ref(buf);
		input->memory.release = [](void *owner) { zst_buffer_unref((zst_buffer_t*)owner); };
		input->destroy = [](zst_buffer_t *buffer) { free(buffer->payload); };
		input->pts = buf->pts; input->dts = buf->dts; input->duration = buf->duration; input->flags = buf->flags;
		zst_result_t result = zst_pad_push(zst_element_get_pad(pad->parent, "src"), input);
		zst_buffer_unref(input);
		return result;
	}
	static zst_element_t *rtsp_audio_adapter_create(rtsp_playback_t *p) {
		static const zst_element_ops_t ops = {.name="rtsp_aac_format_adapter"};
		auto *priv = (rtsp_sink_priv_t*)calloc(1, sizeof(rtsp_sink_priv_t));
		if (!priv) return nullptr;
		priv->playback = p;
		auto *el = zst_element_create(&ops, priv);
		if (!el) { free(priv); return nullptr; }
		for (int i = 0; i < 2; ++i) {
			auto *pad = zst_pad_create(i ? "src" : "sink", i ? ZST_PAD_SRC : ZST_PAD_SINK);
			if (!pad) { zst_element_destroy(el); return nullptr; }
			if (zst_element_add_pad(el, pad) != ZST_OK) { zst_pad_destroy(pad); zst_element_destroy(el); return nullptr; }
			if (!i) pad->push = rtsp_audio_adapter_push;
		}
		return el;
	}
	static zst_result_t rtsp_audio_frame(rtsp_playback_t *p,zst_buffer_t *buf) {
		if(!p||!p->output||!p->output->audio_pcm||!buf||!buf->payload)return ZST_ERROR;
		auto *af=(zst_audio_frame_t*)buf->payload;
		if (af->sample_rate != 48000 || af->channels != 2 || af->format != ZST_AUDIO_FMT_S32LE) return ZST_ERROR;
		rx_audio_playback(p->output, buf, af);
		p->audio_frames++;
		return ZST_OK;
	}
	static zst_result_t rtsp_sink_process(zst_element_t *el,zst_buffer_t *buf,zst_buffer_t **out){
		if(out)*out=nullptr; auto *s=(rtsp_sink_priv_t*)el->priv;
		return s->video?rtsp_video_frame(s->playback,buf):rtsp_audio_frame(s->playback,buf);
	}
	static zst_result_t rtsp_sink_push(zst_pad_t *pad,zst_buffer_t *buf){return rtsp_sink_process(pad->parent,buf,nullptr);}
	static zst_result_t rtsp_video_sink_event(zst_element_t *el, zst_pad_t*, zst_pad_event_t *event) {
		if (!el || !el->priv || !event || event->type != ZST_PAD_EVENT_CAPS) return ZST_OK;
		auto *s = (rtsp_sink_priv_t*)el->priv;
		auto *rx = s->playback->output;
		const zst_caps_t *caps = event->as.caps.caps;
		if (!caps || !caps->structs || strcmp(caps->structs->media_type,"video/x-h264") != 0)
			return ZST_OK;
		const void *avcc = nullptr;
		size_t avcc_size = 0;
		uint32_t width = 0, height = 0;
		double fps = 0.0;
		if (zst_caps_get_buffer(caps,ZST_H26X_PARSER_CAPS_AVCC,&avcc,&avcc_size) != ZST_OK ||
		    !avcc || avcc_size < 11 || avcc_size > INT_MAX ||
		    zst_caps_get_uint(caps,"width",&width) != ZST_OK ||
		    zst_caps_get_uint(caps,"height",&height) != ZST_OK || !width || !height)
			return ZST_OK;
		if (zst_caps_get_double(caps,ZST_H26X_PARSER_CAPS_FRAMERATE,&fps) != ZST_OK ||
		    fps < 1.0 || fps > 240.0) fps = 60.0;
		bool restart = false;
		{
			std::lock_guard<std::mutex> decoder_guard(rx->decoder_lock);
			restart = (rx->decoder_initializing.load(std::memory_order_acquire) ||
			           rx->vdec_started || rx->drm_modeset_active) &&
			          (rx->dec_width != width || rx->dec_height != height || fabs(rx->dec_fps-fps) > 0.005 ||
			           !rx->extra_data || rx->extra_data_size != (int)avcc_size ||
			           memcmp(rx->extra_data,avcc,avcc_size) != 0);
		}
		if (restart) {
			LOGI("RTSP source format changed to %ux%u@%.2f; rebuilding decoder and EDID/VProcSS output",
			     width,height,fps);
			/* CAPS and AUs are delivered serially by the RTSP source thread.
			 * Never hold decoder_lock while joining initialization/callbacks. */
			stop_vdec_decoder(rx);
			stop_vdec_display(rx);
		}
		/* The next IDR uses AVH's common asynchronous DRM/sink initialization:
		 * EDID selects HDMI timing; source dimensions select the scaler rect. */
		return rx_h264_apply_caps(rx,caps);
	}
	static zst_element_t *rtsp_sink_create(rtsp_playback_t *p,bool video){
		static const zst_element_ops_t vops={.name="rtsp_hdmi_video_sink",.process=rtsp_sink_process,.event=rtsp_video_sink_event};
		static const zst_element_ops_t aops={.name="rtsp_hdmi_audio_sink",.process=rtsp_sink_process};
		auto *priv=(rtsp_sink_priv_t*)calloc(1,sizeof(rtsp_sink_priv_t));if(!priv)return nullptr;
		priv->playback=p;priv->video=video;
		auto *el=zst_element_create(video?&vops:&aops,priv);if(!el){free(priv);return nullptr;}
		auto *pad=zst_pad_create("sink",ZST_PAD_SINK);if(!pad||zst_element_add_pad(el,pad)!=ZST_OK){zst_element_destroy(el);return nullptr;}
		pad->push=rtsp_sink_push;return el;
	}
	static int run_rtsp_client(const std::string &url) {
		rtsp_playback_t p; p.output=start_qcap_media_rx(true); if(!p.output)return 1;
		p.output->h264_input_parsed = true;
		p.output->media_source_name = "RTSP";
		auto cleanup=[&](){
			zst_element_t *els[]={p.source,p.vparser,p.aparser,p.adec,p.audio_adapter,p.aresampler,p.vsink,p.asink};
			for(auto *el:els)if(el)zst_element_set_state(el,ZST_STATE_NULL);
			for(auto *el:els)if(el)zst_element_destroy(el);
			stop_qcap_media_rx(p.output);p.output=nullptr;
		};
		p.source=zst_rtsp_source_create(url.c_str()); p.vparser=zst_h26x_parser_create();
		p.aparser=zst_aac_parser_create(); p.adec=zst_aac_decoder_create();
		p.audio_adapter=rtsp_audio_adapter_create(&p); p.aresampler=zst_audio_resampler_create(48000,2,"S32LE");
		p.vsink=rtsp_sink_create(&p,true);p.asink=rtsp_sink_create(&p,false);
		if(!p.source||!p.vparser||!p.aparser||!p.adec||!p.audio_adapter||!p.aresampler||!p.vsink||!p.asink){LOGE("RTSP playback element creation failed");cleanup();return 1;}
		/* Keep this playback instance alive across server restarts and outages. */
		if(zst_element_set_property_bool(p.source,"reconnect",true)!=ZST_OK||
		   zst_element_set_property_int(p.source,"reconnect-delay-ms",1000)!=ZST_OK||
		   zst_element_set_property_int(p.source,"max-reconnect-attempts",-1)!=ZST_OK){LOGE("RTSP reconnect configuration failed");cleanup();return 1;}
		/* The bundled AArch64 floating-point AAC SIMD path corrupts PCM.
		 * Use the independently verified fixed-point backend without changing
		 * process-global FFmpeg CPU flags or the shared media output path. */
		if(zst_element_set_property_string(p.adec,"decoder","aac_fixed")!=ZST_OK){LOGE("RTSP AAC fixed-point decoder configuration failed");cleanup();return 1;}
		if(zst_element_set_property_string(p.vparser,"codec","h264")!=ZST_OK||
		   zst_element_set_property_string(p.vparser,"input-stream-format","byte-stream")!=ZST_OK||
		   zst_element_set_property_string(p.vparser,"aggregate-au","true")!=ZST_OK){LOGE("RTSP H.264 AU parser configuration failed");cleanup();return 1;}
		if(zst_pad_link(zst_element_get_pad(p.source,"video"),zst_element_get_pad(p.vparser,"sink"))!=ZST_OK||
		   zst_pad_link(zst_element_get_pad(p.vparser,"src"),zst_element_get_pad(p.vsink,"sink"))!=ZST_OK||
		   zst_pad_link(zst_element_get_pad(p.source,"audio"),zst_element_get_pad(p.aparser,"sink"))!=ZST_OK||
		   zst_pad_link(zst_element_get_pad(p.aparser,"src"),zst_element_get_pad(p.adec,"sink"))!=ZST_OK||
		   zst_pad_link(zst_element_get_pad(p.adec,"src"),zst_element_get_pad(p.audio_adapter,"sink"))!=ZST_OK||
		   zst_pad_link(zst_element_get_pad(p.audio_adapter,"src"),zst_element_get_pad(p.aresampler,"sink"))!=ZST_OK||
		   zst_pad_link(zst_element_get_pad(p.aresampler,"src"),zst_element_get_pad(p.asink,"sink"))!=ZST_OK){LOGE("RTSP pipeline pad link failed");cleanup();return 1;}
		zst_element_set_state(p.audio_adapter,ZST_STATE_READY);zst_element_set_state(p.aresampler,ZST_STATE_READY);
		zst_element_set_state(p.vparser,ZST_STATE_READY);zst_element_set_state(p.aparser,ZST_STATE_READY);zst_element_set_state(p.adec,ZST_STATE_READY);
		zst_element_set_state(p.vsink,ZST_STATE_READY);zst_element_set_state(p.asink,ZST_STATE_READY);
		zst_element_set_state(p.vparser,ZST_STATE_PLAYING);zst_element_set_state(p.aparser,ZST_STATE_PLAYING);zst_element_set_state(p.adec,ZST_STATE_PLAYING);
		zst_element_set_state(p.audio_adapter,ZST_STATE_PLAYING);zst_element_set_state(p.aresampler,ZST_STATE_PLAYING);
		zst_element_set_state(p.vsink,ZST_STATE_PLAYING);zst_element_set_state(p.asink,ZST_STATE_PLAYING);
		if(zst_element_set_state(p.source,ZST_STATE_PLAYING)!=ZST_OK){LOGE("Could not start RTSP client");cleanup();return 1;}
		LOGI("Independent RTSP client active: H.264 -> shared ALLEGRO2/DRM, AAC -> shared PCM/HDMI: %s",url.c_str());
		LOGI("RTSP reconnect enabled: retry delay=1000 ms, unlimited attempts");
		while(!g_stop_requested){
			sleep(3);
			if(g_stop_requested)break;
			LOGI("RTSP playback: video AUs=%llu decoded frames=%llu decode errors=%llu audio frames=%llu audio playback errors=%llu",
			     (unsigned long long)p.video_frames.load(),(unsigned long long)p.output->decoded_frames.load(),
			     (unsigned long long)p.output->decoded_errors.load(),(unsigned long long)p.audio_frames.load(),
			     (unsigned long long)p.output->audio_playback_errors.load());
		}
		zst_element_set_state(p.source,ZST_STATE_NULL);
		stop_vdec_decoder(p.output);
		LOGI("RTSP client stopped: video AUs=%llu decoded frames=%llu decode errors=%llu audio frames=%llu audio playback errors=%llu unsupported audio periods=%llu",
		     (unsigned long long)p.video_frames.load(),(unsigned long long)p.output->decoded_frames.load(),
		     (unsigned long long)p.output->decoded_errors.load(),(unsigned long long)p.audio_frames.load(),
		     (unsigned long long)p.output->audio_playback_errors.load(),(unsigned long long)p.output->audio_unsupported_periods.load());
		cleanup();return 0;
	}
}

using namespace __sc6f0_dante_rx__;

int main(int argc, char* argv[]) {
	g_argc = argc;
	g_argv = argv;

	modules_init_t _modules_init_;

	std::string dvr_socket = "/var/run/dante/dvr";
	std::string dep_name = "DanteEP";
	std::string channels = "0,1";
	uint32_t sample_rate = 48000;
	uint32_t stat_interval = 3;
	std::string reconstructed_path;
	std::string media_path = "linux-decode";
	std::string custom_mode;
	bool fixed_mode_override = false;
	bool force_dante_source = false;
	uint32_t fixed_w = 3840;
	uint32_t fixed_h = 2160;
	uint32_t fixed_fps = 60;
	bool direct_mode = false;
	bool use_qcap = false;
	bool decode_enabled = true;
	bool decode_requested = false;
	bool recv_only = false;
	bool rtp_stats_only = false;
	bool display_enabled = true;
	std::string rtsp_url;
	uint32_t reorder_window = 1;
	uint32_t reorder_timeout_ms = 1;
	std::string direct_peer;
	std::string direct_local = "192.168.190.145";
	uint16_t direct_port = 16490;

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
			printf("Usage: sc6f0-dante-rx [OPTIONS]\n");
			printf("Options:\n");
			printf("  --rtsp-url <url>  Standalone RTSP H.264/AAC decode to HDMI (no Dante/AVH/Ultra)\n");
			printf("  --use-qcap         Use the QCAP Dante receiver instead of zstreamer (fixed DVR/DEP defaults)\n");
			printf("  --dvr <path>       Path to Dante DVR socket (default: /var/run/dante/dvr)\n");
			printf("  --dep <name>       Dante DEP shared memory name (default: DanteEP)\n");
			printf("  --channels <list>  Audio channels to monitor (default: 0,1)\n");
			printf("  --rate <hz>        Audio sample rate in Hz (default: 48000)\n");
			printf("  --interval <sec>   Periodic statistics interval (default: 3)\n");
			printf("  --media-path <mode>  Media path: 'linux-decode' (default; DVR->VCU->DRM +\n");
			printf("                     DEP->ALSA formatter) or 'ultra-direct' (FPGA VSINK\n");
			printf("                     video + AES3 audio straight to HDMI, no DDR path),\n");
			printf("                     or 'auto' (Ultra default, AVH flow selects VCU)\n");
			printf("  --mode <WxH[@fps]> Override DRM display mode for ultra-direct (e.g. 3840x2160@60)\n");
			printf("  --force-dante-source  Select FPGA DANTE overlay immediately in ultra-direct mode (requires --mode)\n");
			printf("                     If omitted, mode dynamically tracks Dante TX format via DAU\n");
			printf("  --tpg-pattern <id> VTPG background pattern ID for ultra-direct mode\n");
			printf("                     (7=solid black [default], 9=color bars, 0=pass-through)\n");
			printf("  --dump-reconstructed <path>  Write depayloaded Annex-B video AUs\n");
			printf("  --direct <tx-ip>   Direct RTP mode: receive unicast RTP from tx-ip:port,\n");
			printf("                     bypassing the Dante daemon entirely (no decode by default)\n");
			printf("  --decode           Enable the ALLEGRO2 decoder in --direct mode\n");
			printf("  --recv-only        Disable the decoder entirely (verify RTP->packet only)\n");
		printf("  --no-display       Decode-only isolation: skip DRM modeset and vsink display\n");
		printf("                     (VCU decode + pop counting still run)\n");
			printf("  --rtp-stats-only   Measure Dante RTP transport only; disable decode, display, and audio\n");
		printf("  --local-ip <ip>    Local bind address for --direct (default: 0.0.0.0)\n");
		printf("  --port <port>      RTP port for --direct (default: 16490)\n");
		printf("  --reorder-window <n>  RTP reorder window in packets (default: 1; lib default: 64; max: 4096)\n");
		printf("  --reorder-timeout-ms <ms>  RTP reorder wait in ms (default: 1; lib default: 20; max: 10000)\n");
			printf("  -h, --help         Show this help message\n");
			return 0;
		} else if (strcmp(argv[i], "--use-qcap") == 0) {
			use_qcap = true;
		} else if (strcmp(argv[i], "--rtsp-url") == 0) {
			if (i + 1 >= argc) { LOGE("--rtsp-url requires a URL"); return 1; }
			rtsp_url = argv[++i];
		} else if (strcmp(argv[i], "--dvr") == 0 && i + 1 < argc) {
			dvr_socket = argv[++i];
		} else if (strcmp(argv[i], "--dep") == 0 && i + 1 < argc) {
			dep_name = argv[++i];
		} else if (strcmp(argv[i], "--channels") == 0 && i + 1 < argc) {
			channels = argv[++i];
		} else if (strcmp(argv[i], "--rate") == 0 && i + 1 < argc) {
			sample_rate = (uint32_t)strtoul(argv[++i], nullptr, 10);
		} else if (strcmp(argv[i], "--interval") == 0 && i + 1 < argc) {
			stat_interval = (uint32_t)strtoul(argv[++i], nullptr, 10);
		} else if (strcmp(argv[i], "--media-path") == 0 && i + 1 < argc) {
			media_path = argv[++i];
		} else if (strcmp(argv[i], "--tpg-pattern") == 0 && i + 1 < argc) {
			g_tpg_pattern = (uint32_t)strtoul(argv[++i], nullptr, 10);
		} else if (strcmp(argv[i], "--force-dante-source") == 0) {
			force_dante_source = true;
		} else if (strcmp(argv[i], "--mode") == 0 && i + 1 < argc) {
			custom_mode = argv[++i];
			uint32_t w = 0, h = 0, fps = 60;
			if (sscanf(custom_mode.c_str(), "%ux%u@%u", &w, &h, &fps) == 3) {
				fixed_w = w;
				fixed_h = h;
				fixed_fps = fps;
				fixed_mode_override = true;
			} else if (sscanf(custom_mode.c_str(), "%ux%u", &w, &h) == 2) {
				fixed_w = w;
				fixed_h = h;
				fixed_fps = 60;
				fixed_mode_override = true;
			} else {
				LOGE("Invalid --mode format '%s'. Expected WxH[@fps], e.g. 3840x2160@60", custom_mode.c_str());
				return 1;
			}
		} else if (strcmp(argv[i], "--dump-reconstructed") == 0 && i + 1 < argc) {
			reconstructed_path = argv[++i];
		} else if (strcmp(argv[i], "--direct") == 0 && i + 1 < argc) {
			direct_peer = argv[++i];
			direct_mode = true;
		} else if (strcmp(argv[i], "--decode") == 0) {
			decode_requested = true;
		} else if (strcmp(argv[i], "--recv-only") == 0) {
			recv_only = true;
		} else if (strcmp(argv[i], "--no-display") == 0) {
			display_enabled = false;
		} else if (strcmp(argv[i], "--rtp-stats-only") == 0) {
			rtp_stats_only = true;
		} else if (strcmp(argv[i], "--local-ip") == 0 && i + 1 < argc) {
			direct_local = argv[++i];
		} else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
			direct_port = (uint16_t)strtoul(argv[++i], nullptr, 10);
		} else if (strcmp(argv[i], "--reorder-window") == 0 && i + 1 < argc) {
			reorder_window = (uint32_t)strtoul(argv[++i], nullptr, 10);
		} else if (strcmp(argv[i], "--reorder-timeout-ms") == 0 && i + 1 < argc) {
			reorder_timeout_ms = (uint32_t)strtoul(argv[++i], nullptr, 10);
		}
	}

	if (reorder_window < 1 || reorder_window > 4096) {
		LOGE("Invalid --reorder-window %u (expected 1..4096)", reorder_window);
		return 1;
	}
	if (reorder_timeout_ms < 1 || reorder_timeout_ms > 10000) {
		LOGE("Invalid --reorder-timeout-ms %u (expected 1..10000)", reorder_timeout_ms);
		return 1;
	}
	if (!rtsp_url.empty() && (use_qcap || direct_mode || !reconstructed_path.empty() ||
	    recv_only || rtp_stats_only || media_path != "linux-decode" || !display_enabled)) {
		LOGE("--rtsp-url is an independent playback mode; do not combine it with Dante/Ultra/direct decode options");
		return 1;
	}

	decode_enabled = (recv_only || rtp_stats_only) ? false : (direct_mode ? decode_requested : true);
	if (use_qcap &&
	    (direct_mode || !reconstructed_path.empty() || recv_only || rtp_stats_only ||
	     media_path != "linux-decode" || dvr_socket != "/var/run/dante/dvr" ||
	     dep_name != "DanteEP" || channels != "0,1" || sample_rate != 48000)) {
		LOGE("--use-qcap requires the normal Dante route with default --dvr /var/run/dante/dvr, "
		     "--dep DanteEP, --channels 0,1, and --rate 48000; it cannot be combined with "
		     "--direct, --dump-reconstructed, --recv-only, --rtp-stats-only, or ultra-direct media mode");
		return 1;
	}
	if (direct_mode && (media_path == "ultra-direct" || media_path == "auto")) {
		LOGE("--direct cannot be combined with Ultra/auto media paths");
		return 1;
	}
	if (media_path == "auto" && (recv_only || rtp_stats_only || !display_enabled ||
	                            !reconstructed_path.empty())) {
		LOGE("--media-path auto requires AVH decode/display; --recv-only, --rtp-stats-only, --no-display and --dump-reconstructed are unsupported");
		return 1;
	}
	if (force_dante_source && media_path != "auto" &&
	    (media_path != "ultra-direct" || !fixed_mode_override)) {
		LOGE("--force-dante-source requires --media-path ultra-direct and --mode WxH@fps (or --media-path auto)");
		return 1;
	}
	if (media_path != "linux-decode" && media_path != "ultra-direct" && media_path != "auto") {
		LOGE("unknown --media-path '%s' (expected 'linux-decode', 'ultra-direct' or 'auto')",
		     media_path.c_str());
		return 1;
	}

	signal(SIGINT, sig_handler);
	signal(SIGTERM, sig_handler);
	if (!rtsp_url.empty())
		return run_rtsp_client(rtsp_url);

	printf("============================================================\n");
	if (use_qcap) {
		printf(" SC6F0 Dante RX (QCAP API alternative)\n");
		printf(" DVR Socket:    /var/run/dante/dvr (QCAP fixed)\n");
		printf(" DEP SharedMem: DanteEP (QCAP fixed)\n");
		printf(" Video/Audio:   channel 0 / channel 0\n");
		printf(" Decoder:       QCAP ALLEGRO\n");
	} else if (direct_mode) {
		printf(" SC6F0 RX DIRECT RTP (zstreamer internal, no Dante session)\n");
		printf(" Peer (TX):     %s\n", direct_peer.c_str());
		printf(" Local Bind:    %s\n", direct_local.c_str());
		printf(" RTP Port:      %u\n", direct_port);
		printf(" Audio:         disabled\n");
		printf(" Decode:        %s\n", decode_enabled ? "enabled" : "disabled (verify RTP->packet only)");
	} else if (media_path == "ultra-direct" || media_path == "auto") {
		printf(" SC6F0 RX %s (DRM KMS + DAU service)\n",
		       media_path == "auto" ? "AUTO: Ultra -> AVH on flow" : "DANTE ULTRA DIRECT");
		printf(" Video: VSINK -> v_mix layer1 -> HDMI | Audio: AES3 -> SI1 -> HDMI\n");
		if (fixed_mode_override) {
			printf(" Mode:          %ux%u@%u (Fixed override)\n", fixed_w, fixed_h, fixed_fps);
		} else {
			printf(" Mode:          Dynamic (tracks Dante TX format via DAU)\n");
		}
		printf(" TPG Pattern:   %u (%s)\n", g_tpg_pattern,
		       g_tpg_pattern == 7 ? "solid black" :
		       g_tpg_pattern == 9 ? "color bars" :
		       g_tpg_pattern == 0 ? "pass-through" : "custom");
		printf(" DAU Service:   sink0 registered (D-Bus)\n");
		if (media_path == "auto")
			printf(" AVH flow:      DVR %s; start on Ultra even without Ultra subscription\n", dvr_socket.c_str());
		else
			printf(" This mode bypasses DVR socket, DEP, VCU, and ALSA PCM.\n");
	} else {
		printf(" SC6F0 Dante RX (%s)\n", rtp_stats_only ? "RTP transport stats only" : "zstreamer pipeline");
		printf(" DVR Socket:    %s\n", dvr_socket.c_str());
		if (!rtp_stats_only) {
			printf(" DEP SharedMem: %s\n", dep_name.c_str());
			printf(" Channels:      %s\n", channels.c_str());
			printf(" Sample Rate:   %u Hz\n", sample_rate);
		}
		printf(" Decode/display/audio: %s\n", rtp_stats_only ? "disabled" :
		       (decode_enabled ? (display_enabled ? "enabled" : "enabled (decode-only, no display)") : "decoder disabled (--recv-only)"));
		printf(" Reorder: window=%u timeout=%ums\n", reorder_window, reorder_timeout_ms);
	}
	printf(" Stat Interval: %u s\n", stat_interval);
	printf(" Press 'q' + Enter (or Ctrl+C) to stop.\n");
	printf("============================================================\n\n");

	if (media_path == "ultra-direct" || media_path == "auto") {
		bool auto_mode = media_path == "auto";
		return run_ultra_direct(stat_interval, fixed_mode_override,
			force_dante_source || auto_mode, fixed_w, fixed_h, fixed_fps,
			auto_mode, dvr_socket, dep_name, channels, sample_rate,
			reorder_window, reorder_timeout_ms);
	}

	if (use_qcap) {
		qcap_dante_rx_t* rx = start_qcap_dante_rx(display_enabled);
		if (!rx) {
			LOGE("Failed to start Dante RX through the QCAP API.");
			return 1;
		}
		while (!g_stop_requested) {
			pause();
		}
		stop_qcap_dante_rx(rx);
		return 0;
	}

	zst_dante_rx_t* rx = start_dante_rx(dvr_socket, dep_name, channels, sample_rate,
	                                  stat_interval, reconstructed_path,
	                                  decode_enabled, rtp_stats_only, direct_mode, direct_peer,
	                                  direct_local, direct_port, display_enabled,
	                                  reorder_window, reorder_timeout_ms);
	if (!rx) {
		LOGE("Failed to start Dante RX pipeline.");
		return 1;
	}

	while (!g_stop_requested) {
		pause();
	}

	stop_dante_rx(rx);
	return 0;
}
