#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <signal.h>
#include <unistd.h>
#include <drm.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <alsa/asoundlib.h>

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

	/* ── H.264 bitstream utilities ────────────────────────────────────── */

	/* Minimal bitstream reader with RBSP emulation prevention (0x000003 → 0x0000) */
	struct h264_bs_t {
		const uint8_t* rbsp;
		size_t size;
		size_t bits;
	};

	static int bs_read_bit(h264_bs_t& bs) {
		if (bs.bits >= bs.size * 8) return 0;
		size_t byte_idx = bs.bits / 8;
		int bit_offset = 7 - (bs.bits % 8);
		bs.bits++;
		return (bs.rbsp[byte_idx] >> bit_offset) & 1;
	}

	static int bs_read_bits(h264_bs_t& bs, int n) {
		int val = 0;
		for (int i = 0; i < n; i++)
			val = (val << 1) | bs_read_bit(bs);
		return val;
	}

	static int bs_read_ue(h264_bs_t& bs) {
		int leading_zeros = 0;
		while (bs_read_bit(bs) == 0 && leading_zeros < 32)
			leading_zeros++;
		if (leading_zeros == 0) return 0;
		return (1 << leading_zeros) - 1 + bs_read_bits(bs, leading_zeros);
	}

	static int bs_read_se(h264_bs_t& bs) {
		int val = bs_read_ue(bs);
		return (val & 1) ? (val + 1) / 2 : -(val / 2);
	}

	/* Parse SPS NAL unit (after NAL header byte) for width, height, fps.
	 * data must point to the first byte AFTER the NAL type byte (i.e. after 0x67).
	 * Returns 0 on success. */
	static int h264_parse_sps(const uint8_t* data, size_t size,
	                          uint32_t* out_w, uint32_t* out_h, double* out_fps)
	{
		if (!data || size < 4) return -1;
		h264_bs_t bs = { data, size, 0 };

		int profile_idc = bs_read_bits(bs, 8);
		bs_read_bits(bs, 8); /* constraint_set0..5 + reserved_zero_2bits */
		bs_read_bits(bs, 8); /* level_idc */
		bs_read_ue(bs);      /* seq_parameter_set_id */

		int chroma_format_idc = 1; /* default: 4:2:0 */

		/* High profile and above have extra fields */
		if (profile_idc == 100 || profile_idc == 110 || profile_idc == 122 ||
		    profile_idc == 244 || profile_idc ==  44 || profile_idc ==  83 ||
		    profile_idc ==  86 || profile_idc == 118 || profile_idc == 128 ||
		    profile_idc == 138 || profile_idc == 139 || profile_idc == 134) {
			chroma_format_idc = bs_read_ue(bs);
			if (chroma_format_idc == 3)
				bs_read_bit(bs); /* separate_colour_plane_flag */
			bs_read_ue(bs); /* bit_depth_luma_minus8 */
			bs_read_ue(bs); /* bit_depth_chroma_minus8 */
			bs_read_bit(bs); /* qpprime_y_zero_transform_bypass_flag */
			int seq_scaling_matrix_present = bs_read_bit(bs);
			if (seq_scaling_matrix_present) {
				int cnt = (chroma_format_idc != 3) ? 8 : 12;
				for (int i = 0; i < cnt; i++) {
					if (bs_read_bit(bs)) { /* seq_scaling_list_present_flag[i] */
/* skip scaling list — not needed for resolution */
						int last = 8, next = 8;
						for (int j = 0; j < (i < 6 ? 16 : 64); j++) {
							next = last + bs_read_se(bs) + 256;
							last = next;
						}
					}
				}
			}
		}

		bs_read_ue(bs); /* log2_max_frame_num_minus4 */
		int pic_order_cnt_type = bs_read_ue(bs);
		if (pic_order_cnt_type == 0) {
			bs_read_ue(bs); /* log2_max_pic_order_cnt_lsb_minus4 */
		} else if (pic_order_cnt_type == 1) {
			bs_read_bit(bs); /* delta_pic_order_always_zero_flag */
			bs_read_se(bs);  /* offset_for_non_ref_pic */
			bs_read_se(bs);  /* offset_for_top_to_bottom_field */
			int num_ref_frames = bs_read_ue(bs);
			for (int i = 0; i < num_ref_frames; i++)
				bs_read_se(bs); /* offset_for_ref_frame */
		}

		int max_num_ref_frames = bs_read_ue(bs);
		(void)max_num_ref_frames;
		bs_read_bit(bs); /* gaps_in_frame_num_value_allowed_flag */

		int pic_width_in_mbs_minus1 = bs_read_ue(bs);
		int pic_height_in_map_units_minus1 = bs_read_ue(bs);
		int frame_mbs_only_flag = bs_read_bit(bs);
		if (!frame_mbs_only_flag)
			bs_read_bit(bs); /* mb_adaptive_frame_field_flag */

		int direct_8x8_inference_flag = bs_read_bit(bs);
		(void)direct_8x8_inference_flag;

		int frame_cropping_flag = bs_read_bit(bs);
		int crop_left = 0, crop_right = 0, crop_top = 0, crop_bottom = 0;
		if (frame_cropping_flag) {
			crop_left   = bs_read_ue(bs);
			crop_right  = bs_read_ue(bs);
			crop_top    = bs_read_ue(bs);
			crop_bottom = bs_read_ue(bs);
		}

		/* Compute resolution */
		uint32_t w = (uint32_t)(pic_width_in_mbs_minus1 + 1) * 16;
		uint32_t h = (uint32_t)(2 - frame_mbs_only_flag) * (pic_height_in_map_units_minus1 + 1) * 16;

		/* Crop (YUV420: crop units are 2 pixels) */
		int crop_unit_x = 1, crop_unit_y = 2 - frame_mbs_only_flag;
		if (chroma_format_idc == 1) { crop_unit_x = 2; crop_unit_y = 2 * (2 - frame_mbs_only_flag); }
		else if (chroma_format_idc == 2) { crop_unit_x = 2; crop_unit_y = 2 - frame_mbs_only_flag; }
		if (frame_cropping_flag) {
			w -= (crop_left + crop_right) * crop_unit_x;
			h -= (crop_top + crop_bottom) * crop_unit_y;
		}

		/* VUI timing info (optional) */
		double fps = 0.0;
		int vui_parameters_present = bs_read_bit(bs);
		if (vui_parameters_present) {
			int aspect_ratio_info_present = bs_read_bit(bs);
			if (aspect_ratio_info_present) {
				int aspect_ratio_idc = bs_read_bits(bs, 8);
				if (aspect_ratio_idc == 255) { /* Extended_SAR */
					bs_read_bits(bs, 16); /* sar_width */
					bs_read_bits(bs, 16); /* sar_height */
				}
			}
			int overscan_info_present = bs_read_bit(bs);
			if (overscan_info_present) bs_read_bits(bs, 1);
			int video_signal_type_present = bs_read_bit(bs);
			if (video_signal_type_present) {
				bs_read_bits(bs, 3); /* video_format */
				bs_read_bit(bs);     /* video_full_range_flag */
				int colour_desc_present = bs_read_bit(bs);
				if (colour_desc_present) bs_read_bits(bs, 8 + 8 + 8);
			}
			int chroma_loc_info_present = bs_read_bit(bs);
			if (chroma_loc_info_present) {
				bs_read_ue(bs); /* chroma_sample_loc_type_top_field */
				bs_read_ue(bs); /* chroma_sample_loc_type_bottom_field */
			}
			int timing_info_present = bs_read_bit(bs);
			if (timing_info_present) {
				uint32_t num_units_in_tick = bs_read_bits(bs, 32);
				uint32_t time_scale = bs_read_bits(bs, 32);
				if (num_units_in_tick > 0)
					fps = (double)time_scale / (2.0 * (double)num_units_in_tick);
			}
		}

		if (out_w) *out_w = w;
		if (out_h) *out_h = h;
		if (out_fps) *out_fps = fps;
		return 0;
	}

	/* Scan Annex-B bitstream for SPS (type 7) and PPS (type 8) NAL units.
	 * Copies raw NAL bytes (including start code) into output buffers.
	 * Returns 0 if at least SPS was found. */
	static int h264_extract_sps_pps(const uint8_t* data, size_t size,
	                                uint8_t* sps_out, int* sps_size,
	                                uint8_t* pps_out, int* pps_size)
	{
		if (!data || size < 4) return -1;
		int found_sps = 0;
		const int sps_capacity = sps_size ? *sps_size : 0;
		const int pps_capacity = pps_size ? *pps_size : 0;

		if (sps_size) *sps_size = 0;
		if (pps_size) *pps_size = 0;

		size_t i = 0;
		while (i + 3 < size) {
			/* Find start code: 0x000001 or 0x00000001 */
			size_t sc_len = 0;
			if (data[i] == 0 && data[i + 1] == 0) {
				if (i + 2 < size && data[i + 2] == 1)
					sc_len = 3;
				else if (i + 3 < size && data[i + 2] == 0 && data[i + 3] == 1)
					sc_len = 4;
			}
			if (sc_len == 0) { i++; continue; }

			size_t nal_start = i;
			size_t data_start = i + sc_len;
			if (data_start >= size) break;

			uint8_t nal_type = data[data_start] & 0x1F;

			/* Find end of this NAL (next start code or end of buffer) */
			size_t nal_end = size;
			for (size_t j = data_start + 1; j + 2 < size; j++) {
				if (data[j] == 0 && data[j + 1] == 0 &&
				    (data[j + 2] == 1 || (j + 3 < size && data[j + 2] == 0 && data[j + 3] == 1))) {
					nal_end = j;
					break;
				}
			}

			int nal_len = (int)(nal_end - nal_start);
			if (nal_len > 0) {
				if (nal_type == 7 && sps_out && sps_size && nal_len <= sps_capacity) {
					memcpy(sps_out, data + nal_start, (size_t)nal_len);
					*sps_size = nal_len;
					found_sps = 1;
				} else if (nal_type == 8 && pps_out && pps_size && nal_len <= pps_capacity) {
					memcpy(pps_out, data + nal_start, (size_t)nal_len);
					*pps_size = nal_len;
				}
			}

			i = nal_end;
		}
		return found_sps ? 0 : -1;
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
	static void rx_video_bridge_push(rx_video_stats_t* stats, const zst_buffer_t* buf);

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

		/* qcap2 owns H.264 bootstrap, recovery, and decode submission. */
		rx_video_bridge_push(stats, buf);

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

	/* ── Master Dante RX Pipeline Structure ─────────────────────────── */
	struct zst_dante_rx_t {
		zst_pipeline_t* pipe = nullptr;
		zst_scheduler_t* sched = nullptr;
		zst_clock_t* clock = nullptr;
		zst_element_t* session = nullptr;
		zst_element_t* coordinator = nullptr;
		zst_element_t* rx_dep_src = nullptr;
		zst_element_t* rx_video_sink = nullptr;
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
		uint32_t                      drm_primary_fb_id = 0;
		uint32_t                      drm_primary_bo_handle = 0;
		uint32_t                      drm_mode_blob_id = 0;
		bool                          drm_modeset_active = false;
		qcap2_event_t*               vdec_event = nullptr;
		qcap2_event_handlers_t*      vdec_event_handlers = nullptr;
		qcap2_packet_pool_t*         vdec_packet_pool = nullptr;
		bool                         vdec_started = false;
		bool                         vdec_sink_started = false;
		bool                         audio_pcm_started = false;
		bool                         vdec_init_done = false;
		bool                         vdec_extra_set = false;
		uint8_t*                     extra_data = nullptr;
		int                          extra_data_size = 0;
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
		std::mutex                   decoder_lock;
		std::mutex                   audio_lock;
		std::string                  reconstructed_path;
		FILE*                        reconstructed_file = nullptr;

		std::string dvr_socket{"/var/run/dante/dvr"};
		std::string dep_name{"DanteEP"};
		std::string channels{"0,1"};
		uint32_t sample_rate{48000};
		uint32_t stat_interval_sec{3};

		/* Direct RTP verification mode (bypasses the Dante daemon/session). */
		bool direct_mode = false;
		bool decode_enabled = true;
		std::string direct_transmitter;
		std::string direct_local{"0.0.0.0"};
		uint16_t direct_port = 16490;

		pthread_t bus_th = 0;
		pthread_t sched_th = 0;
		std::atomic<bool> running{false};
		uint64_t start_time_ms{0};
	};

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
			samples == 0 || samples > 960 || buf->memory.size != samples * 8) {
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
		if (rx->audio_pending_frame.size() < output_bytes)
			return;

		snd_pcm_sframes_t written = snd_pcm_writei(rx->audio_pcm,
			rx->audio_pending_frame.data(), 960);
		rx->audio_pending_frame.clear();
		if (written != 960 && written != -EAGAIN) {
			if (written < 0 && (written == -EPIPE || written == -ESTRPIPE))
				snd_pcm_prepare(rx->audio_pcm);
			rx->audio_playback_errors++;
		}
	}

	/* ── qcap2 ALLEGRO2 hardware decoder ────────────────────────────── */

	static QRETURN on_vdec_event_callback(PVOID pUserData);

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
		if (!rx->vdec) return QCAP_RT_OK;

		while (true) {
			qcap2_rcbuffer_t* pFrame = nullptr;
			QRESULT qres = qcap2_video_decoder_pop(rx->vdec, &pFrame);
			if (qres != QCAP_RS_SUCCESSFUL) {
				if (qres != QCAP_RS_ERROR_TIMEOUT)
					rx->decoded_errors++;
				break;
			}
			rx->decoded_frames++;
			if (rx->vdec_sink && rx->vdec_sink_started) {
				qres = qcap2_video_sink_push(rx->vdec_sink, pFrame);
				if (qres != QCAP_RS_SUCCESSFUL) {
					rx->decoder_display_errors++;
					LOGE("qcap2_video_sink_push() failed, qres=%d", (int)qres);
				}
			}
			qcap2_rcbuffer_release(pFrame);
		}
		return QCAP_RT_OK;
	}

	/* Allegro2 push/pop support concurrent access; no decoder_lock serialization. */
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

	/* Build an avcC (AVCDecoderConfigurationRecord) from the first AU that
	 * carries SPS+PPS, mirroring the decode fixture's make_avcc(). */
	static void set_vdec_extra_data_once(zst_dante_rx_t* rx,
		const uint8_t* data, size_t data_size) {
		if (rx->vdec_extra_set || !rx->vdec || !data || data_size < 4)
			return;

		const uint8_t* sps = nullptr; size_t sps_len = 0;
		const uint8_t* pps = nullptr; size_t pps_len = 0;
		size_t base = 0;
		while (base + 3 < data_size) {
			size_t sc = 0;
			if (data[base] == 0 && data[base + 1] == 0) {
				if (data[base + 2] == 1) sc = 3;
				else if (base + 4 <= data_size && data[base + 2] == 0 && data[base + 3] == 1) sc = 4;
			}
			if (!sc) { ++base; continue; }
			size_t nal = base + sc;
			if (nal >= data_size) break;
			uint8_t type = data[nal] & 0x1f;
			size_t end = nal + 1;
			while (end + 3 < data_size) {
				if (data[end] == 0 && data[end + 1] == 0 &&
				    (data[end + 2] == 1 ||
				     (end + 4 <= data_size && data[end + 2] == 0 && data[end + 3] == 1)))
					break;
				++end;
			}
			size_t nal_len = end - nal;
			if (type == 7 && !sps) { sps = data + nal; sps_len = nal_len; }
			else if (type == 8 && !pps) { pps = data + nal; pps_len = nal_len; }
			if (sps && pps) break;
			base = end;
		}
		if (!sps || !pps || sps_len > UINT16_MAX || pps_len > UINT16_MAX) return;

		std::vector<uint8_t> avcc;
		avcc.resize(11 + sps_len + pps_len);
		uint8_t* out = avcc.data();
		out[0] = 1;
		out[1] = sps[1];
		out[2] = sps[2];
		out[3] = sps[3];
		out[4] = 0xff;
		out[5] = 0xe1;
		out[6] = (uint8_t)(sps_len >> 8);
		out[7] = (uint8_t)sps_len;
		memcpy(out + 8, sps, sps_len);
		size_t off = 8 + sps_len;
		out[off] = 1;
		out[off + 1] = (uint8_t)(pps_len >> 8);
		out[off + 2] = (uint8_t)pps_len;
		memcpy(out + off + 3, pps, pps_len);

		qcap2_video_decoder_set_extra_data(rx->vdec, avcc.data(), (int)avcc.size());
		rx->vdec_extra_set = true;
		LOGI("ALLEGRO2 decoder extra_data set: avcC=%zu bytes (SPS=%zu, PPS=%zu)",
		     avcc.size(), sps_len, pps_len);
	}

	static int init_vdec_decoder(zst_dante_rx_t* rx) {
		/* 1. Create decoder */
		rx->vdec = qcap2_video_decoder_new();
		if (!rx->vdec) {
			LOGE("qcap2_video_decoder_new() failed");
			return -1;
		}

		/* qcap2 bootstraps dimensions and codec config from the first H.264 IDR. */
		std::shared_ptr<qcap2_video_encoder_property_t> prop(
			qcap2_video_encoder_property_new(), qcap2_video_encoder_property_delete);
		qcap2_video_encoder_property_set_property(prop.get(),
			QCAP_ENCODER_TYPE_ALLEGRO2, QCAP_ENCODER_FORMAT_H264,
			QCAP_COLORSPACE_TYPE_NV12,
			3840, 2160, 60.0,
			0, 0, 0, 0, 0, 0);
		qcap2_video_decoder_set_video_property(rx->vdec, prop.get());

		/* The Dante coordinator provides one complete Annex-B AU per buffer. */
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
		LOGI("qcap2 ALLEGRO2 decoder configured; waiting for H.264 SPS/PPS/IDR");
		return 0;
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
		rx->drm_primary_fb_id = 0;
		rx->drm_primary_bo_handle = 0;
		rx->drm_mode_blob_id = 0;
		rx->drm_modeset_active = false;
	}

	static int start_drm_output(zst_dante_rx_t* rx) {
		const uint32_t video_plane_id = 36;
		const uint32_t width = 3840;
		const uint32_t height = 2160;
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
		std::shared_ptr<drmModePlane> video_plane(drmModeGetPlane(rx->drm_fd, video_plane_id), drmModeFreePlane);
		if (!resources || !planes || !video_plane) {
			LOGE("DRM plane %u or resources are unavailable", video_plane_id);
			stop_drm_output(rx);
			return -1;
		}

		drmModeModeInfo mode = {};
		bool have_mode = false;
		int crtc_index = -1;
		for (int i = 0; i < resources->count_connectors && crtc_index < 0; ++i) {
			std::shared_ptr<drmModeConnector> connector(
				drmModeGetConnector(rx->drm_fd, resources->connectors[i]), drmModeFreeConnector);
			if (!connector || connector->connection != DRM_MODE_CONNECTED) continue;
			drmModeModeInfo connector_mode = {};
			bool connector_has_mode = false;
			for (int m = 0; m < connector->count_modes; ++m) {
				if (connector->modes[m].hdisplay == width && connector->modes[m].vdisplay == height &&
					connector->modes[m].vrefresh == 60) {
					connector_mode = connector->modes[m];
					connector_has_mode = true;
					break;
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
					crtc_index = c;
					break;
				}
			}
		}
		if (crtc_index < 0 || !have_mode) {
			LOGE("no connected 3840x2160@60 HDMI path supports DRM plane %u", video_plane_id);
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
		create.width = width;
		create.height = height;
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
		if (drmModeAddFB2(rx->drm_fd, width, height, DRM_FORMAT_BGR888, handles, pitches, offsets,
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
		drmModeAtomicAddProperty(request.get(), rx->drm_primary_plane_id, crtc_w, width);
		drmModeAtomicAddProperty(request.get(), rx->drm_primary_plane_id, crtc_h, height);
		drmModeAtomicAddProperty(request.get(), rx->drm_primary_plane_id, src_x, 0);
		drmModeAtomicAddProperty(request.get(), rx->drm_primary_plane_id, src_y, 0);
		drmModeAtomicAddProperty(request.get(), rx->drm_primary_plane_id, src_w, width << 16);
		drmModeAtomicAddProperty(request.get(), rx->drm_primary_plane_id, src_h, height << 16);
		if (drmModeAtomicCommit(rx->drm_fd, request.get(), DRM_MODE_ATOMIC_ALLOW_MODESET, nullptr)) {
			LOGE("DRM HDMI modeset failed, err=%d", errno);
			stop_drm_output(rx);
			return -1;
		}
		rx->drm_modeset_active = true;
		LOGI("DRM HDMI active: connector %u, CRTC %u, primary plane %u, video plane %u",
			rx->drm_connector_id, rx->drm_crtc_id, rx->drm_primary_plane_id, video_plane_id);
		return 0;
	}

	static int start_vdec_sink(zst_dante_rx_t* rx) {
		const uint32_t drm_plane_id = 36;
		qcap2_video_sink_t* sink = qcap2_video_sink_new();
		if (!sink) {
			LOGE("qcap2_video_sink_new() failed");
			return -1;
		}

		qcap2_video_sink_set_backend_type(sink, QCAP2_VIDEO_SINK_BACKEND_TYPE_XLNX2);
		qcap2_video_sink_set_native_handle(sink,
			QCAP_HWND_DRM_PLANE_ID_MASK | drm_plane_id);
		std::shared_ptr<qcap2_video_format_t> format(
			qcap2_video_format_new(), qcap2_video_format_delete);
		qcap2_video_format_set_property(format.get(), QCAP_COLORSPACE_TYPE_NV12,
			3840, 2160, FALSE, 60.0);
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
		LOGI("ALLEGRO2 decoder output is displayed on DRM plane %u", drm_plane_id);
		return 0;
	}

	static void rx_video_bridge_push(rx_video_stats_t* stats, const zst_buffer_t* buf) {
		zst_dante_rx_t* rx = stats->rx;
		const uint8_t* data = (const uint8_t*)buf->memory.data;
		if (!rx || !rx->vdec || !data || buf->memory.size < 4)
			return;
		rx->decoder_input_packets++;
		if (rx->reconstructed_file) {
			if (fwrite(data, 1, buf->memory.size, rx->reconstructed_file) != buf->memory.size ||
			    fflush(rx->reconstructed_file) != 0) {
				LOGE("Failed to write reconstructed H.264 AU to %s",
				     rx->reconstructed_path.c_str());
				fclose(rx->reconstructed_file);
				rx->reconstructed_file = nullptr;
			}
		}
		if (buf->flags & ZST_BUFFER_FLAG_DROP) {
			rx->transport_dropped_packets++;
			qcap2_video_decoder_notify_discontinuity(rx->vdec);
		}
		set_vdec_extra_data_once(rx, data, buf->memory.size);
		submit_vdec_packet(rx, data, buf->memory.size,
			(buf->flags & ZST_BUFFER_FLAG_KEYFRAME) != 0, buf->pts);
	}

	#if 0 // Superseded by qcap2 automatic H.264 bootstrap.
	static void rx_video_feed_decoder(rx_video_stats_t* stats,
	                                   const uint8_t* data, size_t data_size,
	                                   uint32_t flags, uint64_t pts) {
		zst_dante_rx_t* rx = stats->rx;
		if (!rx || !data || data_size < 4)
			return;
		rx->decoder_input_packets++;
		if (flags & ZST_BUFFER_FLAG_DROP) {
			std::lock_guard<std::mutex> decoder_guard(rx->decoder_lock);
			rx->decoder_dropped_packets++;
			if (rx->vdec_started)
				qcap2_video_decoder_notify_discontinuity(rx->vdec);
			return;
		}
		/* The scheduler has multiple workers. Keep decoder initialization and
		 * input submission ordered even if upstream pushes overlap. */
		std::lock_guard<std::mutex> decoder_guard(rx->decoder_lock);
		if (rx->reconstructed_file) {
			if (fwrite(data, 1, data_size, rx->reconstructed_file) != data_size ||
			    fflush(rx->reconstructed_file) != 0) {
				LOGE("Failed to write reconstructed H.264 AU to %s",
				     rx->reconstructed_path.c_str());
				fclose(rx->reconstructed_file);
				rx->reconstructed_file = nullptr;
			}
		}

		/* The upstream zst_rtp_depayloader never sets ZST_BUFFER_FLAG_KEYFRAME
		 * on its output buffers, so the buffer flag cannot gate decoder
		 * startup.  Detect the actual NAL content instead. */
		int has_idr = 0;
		int has_sps = 0;
		for (size_t i = 0; i + 3 < data_size; i++) {
			if (data[i] == 0 && data[i + 1] == 0 &&
			    (data[i + 2] == 1 || (i + 4 <= data_size && data[i + 2] == 0 && data[i + 3] == 1))) {
				size_t off = (data[i + 2] == 1) ? i + 3 : i + 4;
				if (off < data_size) {
					uint8_t nal_type = data[off] & 0x1F;
					if (nal_type == 7) has_sps = 1;
					else if (nal_type == 5) has_idr = 1;
				}
			}
		}
		int is_keyframe = has_idr || (flags & ZST_BUFFER_FLAG_KEYFRAME);

		/* Decoder input is submitted directly; failed submissions are dropped. */
		if (rx->vdec_started) {
			submit_vdec_packet(rx, data, data_size, is_keyframe != 0, pts);
			return;
		}

		/* Not started yet.  SPS/PPS may only appear once at the very start of
		 * the stream (sent as a separate SPS+PPS AU, or inline with the first
		 * IDR), so wait for the first buffer that actually carries an SPS —
		 * never gate on ZST_BUFFER_FLAG_KEYFRAME. */
		if (!has_sps) {
			uint64_t waited = stats->no_sps_count.fetch_add(1, std::memory_order_relaxed) + 1;
			if (waited % 300 == 0) {
				LOGW("ALLEGRO2 decoder not started: %lu buffers without SPS/PPS received"
				     " (%lu video pkts, %lu bytes) — is the TX emitting keyframes?",
				     (unsigned long)waited,
				     (unsigned long)stats->total_packets.load(),
				     (unsigned long)stats->total_bytes.load());
			}
			return;
		}
		stats->no_sps_count.store(0, std::memory_order_relaxed);

		/* Copy out SPS+PPS from this buffer */
		int sps_len = 256 * 1024;
		int pps_len = 256 * 1024;
		uint8_t* sps_buf = (uint8_t*)malloc((size_t)sps_len);
		uint8_t* pps_buf = (uint8_t*)malloc((size_t)pps_len);
		if (!sps_buf || !pps_buf) {
			free(sps_buf);
			free(pps_buf);
			return;
		}

		if (h264_extract_sps_pps(data, data_size, sps_buf, &sps_len, pps_buf, &pps_len) != 0) {
			LOGE("ALLEGRO2 decoder init: SPS was detected but SPS/PPS extraction failed");
			free(sps_buf);
			free(pps_buf);
			return;
		}

		/* Locate SPS NAL body (past start code + NAL header byte) */
		size_t sps_nal_off = 0;
		for (size_t i = 0; i + 3 < (size_t)sps_len; i++) {
			if (sps_buf[i] == 0 && sps_buf[i+1] == 0 &&
			    (sps_buf[i+2] == 1 || sps_buf[i+2] == 0)) {
				size_t sc_len = (sps_buf[i+2] == 1) ? 3 : 4;
				uint8_t nal_type = sps_buf[i + sc_len] & 0x1F;
				if (nal_type == 7) {
					sps_nal_off = i + sc_len + 1;
					break;
				}
			}
		}

		if (sps_nal_off == 0) {
			LOGE("ALLEGRO2 decoder init: extracted SPS has no valid Annex-B SPS NAL");
			free(sps_buf);
			free(pps_buf);
			return;
		}

		double fps = 0.0;
		if (h264_parse_sps(sps_buf + sps_nal_off, (size_t)sps_len - sps_nal_off,
		                    &rx->dec_width, &rx->dec_height, &fps) != 0) {
			LOGE("ALLEGRO2 decoder init: SPS parse failed");
			free(sps_buf);
			free(pps_buf);
			return;
		}
		rx->dec_fps = (fps >= 1.0 && fps <= 240.0) ? fps : 60.0;
		if (rx->dec_width == 0 || rx->dec_height == 0) {
			LOGE("ALLEGRO2 decoder init: SPS produced invalid dimensions %ux%u",
			     rx->dec_width, rx->dec_height);
			free(sps_buf);
			free(pps_buf);
			return;
		}

		/* ALLEGRO2 expects an AVCDecoderConfigurationRecord (avcC), not
		 * Annex-B start-code-delimited parameter sets. */
		size_t sps_nal_start = sps_nal_off - 1;
		size_t pps_nal_start = 0;
		for (size_t i = 0; i + 3 < (size_t)pps_len; i++) {
			if (pps_buf[i] == 0 && pps_buf[i + 1] == 0 &&
			    (pps_buf[i + 2] == 1 ||
			     (i + 4 <= (size_t)pps_len && pps_buf[i + 2] == 0 && pps_buf[i + 3] == 1))) {
				pps_nal_start = i + (pps_buf[i + 2] == 1 ? 3 : 4);
				break;
			}
		}
		if (sps_nal_start >= (size_t)sps_len || pps_nal_start >= (size_t)pps_len ||
		    (size_t)sps_len - sps_nal_start < 4 || (size_t)pps_len - pps_nal_start < 1 ||
		    (size_t)sps_len - sps_nal_start > UINT16_MAX ||
		    (size_t)pps_len - pps_nal_start > UINT16_MAX) {
			LOGE("ALLEGRO2 decoder init: invalid SPS/PPS NAL boundaries");
			free(sps_buf);
			free(pps_buf);
			return;
		}
		size_t sps_nal_len = (size_t)sps_len - sps_nal_start;
		size_t pps_nal_len = (size_t)pps_len - pps_nal_start;
		size_t extra_sz = 11 + sps_nal_len + pps_nal_len;
		rx->extra_data = (uint8_t*)malloc(extra_sz);
		if (!rx->extra_data) {
			free(sps_buf);
			free(pps_buf);
			return;
		}
		uint8_t* avcc = rx->extra_data;
		avcc[0] = 1;
		avcc[1] = sps_buf[sps_nal_start + 1];
		avcc[2] = sps_buf[sps_nal_start + 2];
		avcc[3] = sps_buf[sps_nal_start + 3];
		avcc[4] = 0xff; /* four-byte NAL length fields */
		avcc[5] = 0xe1; /* one SPS */
		avcc[6] = (uint8_t)(sps_nal_len >> 8);
		avcc[7] = (uint8_t)sps_nal_len;
		memcpy(avcc + 8, sps_buf + sps_nal_start, sps_nal_len);
		size_t pps_offset = 8 + sps_nal_len;
		avcc[pps_offset] = 1; /* one PPS */
		avcc[pps_offset + 1] = (uint8_t)(pps_nal_len >> 8);
		avcc[pps_offset + 2] = (uint8_t)pps_nal_len;
		memcpy(avcc + pps_offset + 3, pps_buf + pps_nal_start, pps_nal_len);
		rx->extra_data_size = (int)extra_sz;
		free(sps_buf);
		free(pps_buf);

		LOGI("ALLEGRO2 decoder init: SPS=%d bytes, PPS=%d bytes, %ux%u @ %.1f fps",
		     sps_len, pps_len, rx->dec_width, rx->dec_height, rx->dec_fps);

		/* Create event/packet-pool resources before binding and starting decoder. */
		if (SetupVdecAsync(rx) != 0) {
			free(rx->extra_data); rx->extra_data = nullptr; rx->extra_data_size = 0;
			return;
		}
		if (init_vdec_decoder(rx) != 0) {
			StopVdecAsync(rx);
			free(rx->extra_data); rx->extra_data = nullptr; rx->extra_data_size = 0;
			return;
		}

		LOGI("ALLEGRO2 decoder: %ux%u @ %.1f fps, extra_data=%d bytes",
		     rx->dec_width, rx->dec_height, rx->dec_fps, rx->extra_data_size);

		/* Submit the SPS/IDR AU that initialized this decoder. */
		submit_vdec_packet(rx, data, data_size, is_keyframe != 0, pts);
	}

	#endif

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
				} else if (ev->type == ZST_EVENT_DANTE_FLOW_DELETED) {
					const zst_dante_flow_t* f = &ev->as.dante_flow.flow;
					LOGI("[DANTE BUS] RX video flow DELETED: idx=%u", f->flow_index);
					zst_dante_video_coordinator_remove_flow(rx->coordinator, f);
				} else if (ev->type == ZST_EVENT_DANTE_CONNECTED) {
					LOGI("[DANTE BUS] Dante session CONNECTED to DVR socket");
				} else if (ev->type == ZST_EVENT_WARNING) {
					LOGW("[DANTE BUS] Warning: %s", ev->as.warning.message ? ev->as.warning.message : "");
				} else if (ev->type == ZST_EVENT_ERROR) {
					LOGE("[DANTE BUS] Error: %s", ev->as.error.message ? ev->as.error.message : "");
				}
				zst_event_destroy(ev);
			}

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
				uint32_t active_flows = zst_dante_video_coordinator_get_flow_count(rx->coordinator);

				uint64_t rtp_in = get_uint_prop(rx->coordinator, "rx-in-packets");
				uint64_t rtp_lost = get_uint_prop(rx->coordinator, "rx-lost-packets");
				uint64_t rtp_old = get_uint_prop(rx->coordinator, "rx-old-packets");
				uint64_t rtp_dup = get_uint_prop(rx->coordinator, "rx-dup-packets");
				uint64_t parse_fail = get_uint_prop(rx->coordinator, "rx-parse-fail");
				uint64_t dep_in = get_uint_prop(rx->coordinator, "rx-dep-in-packets");
				uint64_t dep_dropped = get_uint_prop(rx->coordinator, "rx-dep-dropped");
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
				uint64_t recovery_skipped = rx->vdec
					? qcap2_video_decoder_get_recovery_skipped_packets(rx->vdec) : 0;
				uint64_t decoded = rx->decoded_frames.load();
				double decoded_fps = dt_sec > 0 ? (decoded - last_decoded) / dt_sec : 0.0;
				last_decoded = decoded;
				double dec_drop_rate = dec_input > 0
					? 100.0 * (double)dec_dropped / (double)dec_input : 0.0;
				LOGI("[DANTE RX DECODER] input=%lu pushed=%lu dropped=%lu (%.2f%%: transport=%lu, pool=%lu, push=%lu, recovery=%lu) decoded=%lu (%.1f fps)",
					(unsigned long)dec_input, (unsigned long)dec_pushed,
					(unsigned long)dec_dropped, dec_drop_rate, (unsigned long)transport_dropped,
					(unsigned long)pool_dropped, (unsigned long)push_failed,
					(unsigned long)recovery_skipped,
					(unsigned long)decoded, decoded_fps);

				if (active_flows > 0 || cur_vpkts > 0) {
					LOGI("[DANTE RX STATS] flows=%u | VIDEO: %lu AUs (%.2f MB, %.1f fps, %.2f Mbps) | RTP: %lu udp-in, %lu reord-in (%.0f/s), lost=%lu, old=%lu, dup=%lu, parse-fail=%lu, dep-in=%lu, dep-drop=%lu, qof=%lu, qd=%lu | AUDIO: %u Hz, %u ch, fmt=%u, %lu periods (%.2f MB, %.2f Mbps, %lu samples, %.1f p/s, underflow=%u, overrun=%u)",
						active_flows,
						(unsigned long)cur_vpkts,
						(double)cur_vbytes / (1024.0 * 1024.0),
						fps, mbps,
						(unsigned long)udp_in,
						(unsigned long)rtp_in, rtp_rate,
						(unsigned long)rtp_lost,
						(unsigned long)rtp_old,
						(unsigned long)rtp_dup,
						(unsigned long)parse_fail,
						(unsigned long)dep_in,
						(unsigned long)dep_dropped,
						(unsigned long)udp_qoverflow,
						(unsigned long)udp_qdepth,
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
	                                     bool direct_mode = false,
	                                     const std::string& direct_peer = "",
	                                     const std::string& direct_local = "0.0.0.0",
	                                     uint16_t direct_port = 16490)
	{
		zst_dante_rx_t* rx = new zst_dante_rx_t();
		rx->dvr_socket = dvr_socket;
		rx->dep_name = dep_name;
		rx->channels = channels;
		rx->sample_rate = sample_rate;
		rx->stat_interval_sec = stat_interval > 0 ? stat_interval : 3;
		rx->reconstructed_path = reconstructed_path;
		rx->decode_enabled = decode_enabled;
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
		/* The default 64-packet/20-ms window is too short for bursty QDMA delivery. */
		set_uint(rx->coordinator, ZST_DANTE_VIDEO_COORDINATOR_PROP_REORDER_WINDOW, 512);
		set_uint(rx->coordinator, ZST_DANTE_VIDEO_COORDINATOR_PROP_REORDER_TIMEOUT_MS, 100);
		rx->rx_video_pad = zst_dante_video_coordinator_request_rx_output_pad(rx->coordinator, 0);
		if (!rx->rx_video_pad) {
			LOGE("zst_dante_video_coordinator_request_rx_output_pad() failed");
			zst_element_destroy(rx->coordinator);
			zst_element_destroy(rx->session);
			zst_pipeline_destroy(rx->pipe);
			delete rx;
			return nullptr;
		}

		/* 3. RX Video Packet Inspector → feeds ALLEGRO2 decoder */
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
		zst_pad_link(rx->rx_video_pad, zst_element_get_pad(rx->rx_video_sink, "sink"));

		/* 4. Dante DEP Audio Source (skipped in direct RTP mode) */
		if (!direct_mode) {
			rx->rx_dep_src = zst_dante_dep_audio_source_create();
			if (!rx->rx_dep_src) {
				LOGE("zst_dante_dep_audio_source_create() failed");
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
			if (start_hdmi_audio_sink(rx) != 0) {
				zst_element_destroy(rx->rx_audio_sink);
				zst_element_destroy(rx->rx_dep_src);
				zst_element_destroy(rx->rx_video_sink);
				zst_element_destroy(rx->coordinator);
				if (rx->session) zst_element_destroy(rx->session);
				zst_pipeline_destroy(rx->pipe);
				delete rx;
				return nullptr;
			}
		}

		if (decode_enabled &&
		    (start_drm_output(rx) != 0 || start_vdec_sink(rx) != 0 ||
		     SetupVdecAsync(rx) != 0 || init_vdec_decoder(rx) != 0)) {
			LOGE("qcap2 video decoder setup failed");
			StopVdecAsync(rx);
			if (rx->vdec_sink) {
				if (rx->vdec_sink_started)
					qcap2_video_sink_stop(rx->vdec_sink);
				qcap2_video_sink_delete(rx->vdec_sink);
				rx->vdec_sink = nullptr;
				rx->vdec_sink_started = false;
			}
			stop_drm_output(rx);
			stop_hdmi_audio_sink(rx);
			if (rx->rx_audio_sink) zst_element_destroy(rx->rx_audio_sink);
			if (rx->rx_dep_src) zst_element_destroy(rx->rx_dep_src);
			zst_element_destroy(rx->rx_video_sink);
			zst_element_destroy(rx->coordinator);
			if (rx->session) zst_element_destroy(rx->session);
			zst_pipeline_destroy(rx->pipe);
			delete rx;
			return nullptr;
		}

		/* Add elements to pipeline */
		zst_pipeline_add(rx->pipe, rx->coordinator);
		zst_pipeline_add(rx->pipe, rx->rx_video_sink);
		if (!direct_mode) {
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
				LOGI("[DIRECT RX] listening on %s:%u from %s (reorder 512/100ms)",
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
		uint64_t recovery_skipped = rx->vdec
			? qcap2_video_decoder_get_recovery_skipped_packets(rx->vdec) : 0;
		double dec_drop_rate = dec_input > 0
			? 100.0 * (double)dec_dropped / (double)dec_input : 0.0;

		/* Stop output callbacks before stopping/deleting the decoder. */
		if (rx->vdec_event_handlers) {
			qcap2_event_handlers_stop(rx->vdec_event_handlers);
			qcap2_event_handlers_delete(rx->vdec_event_handlers);
			rx->vdec_event_handlers = nullptr;
		}
		if (rx->vdec) {
			if (rx->vdec_started)
				qcap2_video_decoder_stop(rx->vdec);
			qcap2_video_decoder_delete(rx->vdec);
			rx->vdec = nullptr;
			rx->vdec_started = false;
		}
		if (rx->vdec_sink) {
			if (rx->vdec_sink_started)
				qcap2_video_sink_stop(rx->vdec_sink);
			qcap2_video_sink_delete(rx->vdec_sink);
			rx->vdec_sink = nullptr;
			rx->vdec_sink_started = false;
		}
		stop_drm_output(rx);
		StopVdecAsync(rx);
		if (rx->extra_data) {
			free(rx->extra_data);
			rx->extra_data = nullptr;
			rx->extra_data_size = 0;
		}

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
		g_stop_requested = 1;
	}

	static int run_ultra_direct(uint32_t stat_interval) {
		if (stat_interval == 0)
			stat_interval = 3;
		LOGI("[ULTRA DIRECT] Dante Ultra FPGA media path: VSINK video (v_mix layer 1) and AES3 audio (SI1) -> HDMI");
		LOGI("[ULTRA DIRECT] This mode does not create a Linux DDR/VCU/DEP/ALSA data path.");
		if (select_hdmi_audio_route(1) != 0) {
			LOGE("[ULTRA DIRECT] failed to select SI1 (Custom FPGA / Dante RX) audio route");
			return 1;
		}
		int initial = read_hdmi_audio_route();
		LOGI("[ULTRA DIRECT] HDMI audio route set to SI1 (%s)",
		     hdmi_audio_route_name(initial));

		uint64_t last_report_ms = 0;
		while (!g_stop_requested) {
			struct timespec ts;
			clock_gettime(CLOCK_MONOTONIC, &ts);
			uint64_t now_ms = (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
			if (now_ms - last_report_ms >= (uint64_t)stat_interval * 1000) {
				last_report_ms = now_ms;
				int route = read_hdmi_audio_route();
				LOGI("[ULTRA DIRECT] status: HDMI audio route=%d (%s) | formatter PCM untouched (SI0 dormant) | Dante runtime owns VSINK/HDMI",
				     route, hdmi_audio_route_name(route));
			}
			usleep(200000);
		}
		LOGI("[ULTRA DIRECT] stopping: leaving HDMI audio route on SI1 (Dante RX).");
		return 0;
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
	bool direct_mode = false;
	bool decode_enabled = true;
	bool decode_requested = false;
	bool recv_only = false;
	std::string direct_peer;
	std::string direct_local = "192.168.190.145";
	uint16_t direct_port = 16490;

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
			printf("Usage: sc6f0-dante-rx [OPTIONS]\n");
			printf("Options:\n");
			printf("  --dvr <path>       Path to Dante DVR socket (default: /var/run/dante/dvr)\n");
			printf("  --dep <name>       Dante DEP shared memory name (default: DanteEP)\n");
			printf("  --channels <list>  Audio channels to monitor (default: 0,1)\n");
			printf("  --rate <hz>        Audio sample rate in Hz (default: 48000)\n");
			printf("  --interval <sec>   Periodic statistics interval (default: 3)\n");
			printf("  --media-path <mode>  Media path: 'linux-decode' (default; DVR->VCU->DRM +\n");
			printf("                     DEP->ALSA formatter) or 'ultra-direct' (FPGA VSINK\n");
			printf("                     video + AES3 audio straight to HDMI, no DDR path)\n");
			printf("  --dump-reconstructed <path>  Write depayloaded Annex-B video AUs\n");
			printf("  --direct <tx-ip>   Direct RTP mode: receive unicast RTP from tx-ip:port,\n");
			printf("                     bypassing the Dante daemon entirely (no decode by default)\n");
			printf("  --decode           Enable the ALLEGRO2 decoder in --direct mode\n");
			printf("  --recv-only        Disable the decoder entirely (verify RTP->packet only)\n");
			printf("  --local-ip <ip>    Local bind address for --direct (default: 0.0.0.0)\n");
			printf("  --port <port>      RTP port for --direct (default: 16490)\n");
			printf("  -h, --help         Show this help message\n");
			return 0;
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
		} else if (strcmp(argv[i], "--dump-reconstructed") == 0 && i + 1 < argc) {
			reconstructed_path = argv[++i];
		} else if (strcmp(argv[i], "--direct") == 0 && i + 1 < argc) {
			direct_peer = argv[++i];
			direct_mode = true;
		} else if (strcmp(argv[i], "--decode") == 0) {
			decode_requested = true;
		} else if (strcmp(argv[i], "--recv-only") == 0) {
			recv_only = true;
		} else if (strcmp(argv[i], "--local-ip") == 0 && i + 1 < argc) {
			direct_local = argv[++i];
		} else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
			direct_port = (uint16_t)strtoul(argv[++i], nullptr, 10);
		}
	}

	decode_enabled = recv_only ? false : (direct_mode ? decode_requested : true);
	if (direct_mode && media_path == "ultra-direct") {
		LOGE("--direct and --media-path ultra-direct are mutually exclusive");
		return 1;
	}
	if (media_path != "linux-decode" && media_path != "ultra-direct") {
		LOGE("unknown --media-path '%s' (expected 'linux-decode' or 'ultra-direct')",
		     media_path.c_str());
		return 1;
	}

	signal(SIGINT, sig_handler);
	signal(SIGTERM, sig_handler);

	printf("============================================================\n");
	if (direct_mode) {
		printf(" SC6F0 RX DIRECT RTP (zstreamer internal, no Dante session)\n");
		printf(" Peer (TX):     %s\n", direct_peer.c_str());
		printf(" Local Bind:    %s\n", direct_local.c_str());
		printf(" RTP Port:      %u\n", direct_port);
		printf(" Audio:         disabled\n");
		printf(" Decode:        %s\n", decode_enabled ? "enabled" : "disabled (verify RTP->packet only)");
	} else if (media_path == "ultra-direct") {
		printf(" SC6F0 RX DANTE ULTRA DIRECT (FPGA passthrough, no zstreamer)\n");
		printf(" Video: VSINK -> v_mix layer1 -> HDMI | Audio: AES3 -> SI1 -> HDMI\n");
		printf(" This mode does not open the DVR socket, DEP, VCU, DRM or ALSA PCM.\n");
	} else {
		printf(" SC6F0 Dante RX (zstreamer pipeline, non-decode mode)\n");
		printf(" DVR Socket:    %s\n", dvr_socket.c_str());
		printf(" DEP SharedMem: %s\n", dep_name.c_str());
		printf(" Channels:      %s\n", channels.c_str());
		printf(" Sample Rate:   %u Hz\n", sample_rate);
		printf(" Decode:        %s\n", decode_enabled ? "enabled" : "disabled (--recv-only)");
	}
	printf(" Stat Interval: %u s\n", stat_interval);
	printf(" Press 'q' + Enter (or Ctrl+C) to stop.\n");
	printf("============================================================\n\n");

	if (media_path == "ultra-direct") {
		return run_ultra_direct(stat_interval);
	}

	zst_dante_rx_t* rx = start_dante_rx(dvr_socket, dep_name, channels, sample_rate,
	                                  stat_interval, reconstructed_path,
	                                  decode_enabled, direct_mode, direct_peer,
	                                  direct_local, direct_port);
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
