#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <signal.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

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

	/* ── Video statistics & custom sink ──────────────────────────────── */
	struct rx_video_stats_t {
		std::atomic<uint64_t> total_packets{0};
		std::atomic<uint64_t> total_bytes{0};
		std::atomic<uint64_t> keyframes{0};
		std::atomic<uint64_t> sps_pps{0};
		std::atomic<uint64_t> idr_slices{0};
		std::atomic<uint64_t> non_idr_slices{0};
		std::atomic<uint64_t> last_pts{0};
		std::atomic<uint64_t> first_pts{0};
	};

	static zst_result_t rx_video_sink_push(zst_pad_t* pad, zst_buffer_t* buf) {
		if (!pad || !pad->parent || !buf) return ZST_ERROR;
		rx_video_stats_t* stats = (rx_video_stats_t*)pad->parent->priv;
		if (!stats) return ZST_ERROR;

		stats->total_packets++;
		stats->total_bytes += buf->memory.size;
		if (stats->first_pts == 0 && buf->pts > 0) {
			stats->first_pts = buf->pts;
		}
		stats->last_pts = buf->pts;

		if (buf->flags & ZST_BUFFER_FLAG_KEYFRAME) {
			stats->keyframes++;
		}

		/* Inspect H.264 NAL type if packet payload is present */
		if (buf->memory.data && buf->memory.size >= 4) {
			const uint8_t* d = (const uint8_t*)buf->memory.data;
			size_t sz = buf->memory.size;
			for (size_t i = 0; i + 3 < sz && i < 32; i++) {
				if (d[i] == 0 && d[i + 1] == 0 &&
				    (d[i + 2] == 1 || (i + 4 <= sz && d[i + 2] == 0 && d[i + 3] == 1))) {
					size_t offset = (d[i + 2] == 1) ? i + 3 : i + 4;
					if (offset < sz) {
						uint8_t nal_type = d[offset] & 0x1F;
						if (nal_type == 7 || nal_type == 8) stats->sps_pps++;
						else if (nal_type == 5) stats->idr_slices++;
						else if (nal_type == 1) stats->non_idr_slices++;
					}
					break;
				}
			}
		}

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
	struct rx_audio_stats_t {
		std::atomic<uint64_t> total_periods{0};
		std::atomic<uint64_t> total_samples{0};
		std::atomic<uint64_t> total_bytes{0};
		std::atomic<uint64_t> last_pts{0};
		std::atomic<uint64_t> first_pts{0};
	};

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
		} else {
			/* Standard DEP default: 2ch 32-bit (8 bytes/frame) */
			stats->total_samples += buf->memory.size / 8;
		}

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

		std::string dvr_socket{"/var/run/dante/dvr"};
		std::string dep_name{"DanteEP"};
		std::string channels{"0,1"};
		uint32_t sample_rate{48000};
		uint32_t stat_interval_sec{3};

		pthread_t bus_th = 0;
		pthread_t sched_th = 0;
		std::atomic<bool> running{false};
		uint64_t start_time_ms{0};
	};

	static void* dante_rx_bus_worker(void* arg) {
		zst_dante_rx_t* rx = (zst_dante_rx_t*)arg;
		uint64_t last_stat_time = 0;
		uint64_t last_vpkts = 0;
		uint64_t last_vbytes = 0;
		uint64_t last_apds = 0;

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

				uint64_t delta_vpkts = cur_vpkts - last_vpkts;
				uint64_t delta_vbytes = cur_vbytes - last_vbytes;
				last_vpkts = cur_vpkts;
				last_vbytes = cur_vbytes;

				double fps = dt_sec > 0 ? (double)delta_vpkts / dt_sec : 0.0;
				double mbps = dt_sec > 0 ? ((double)delta_vbytes * 8.0) / (dt_sec * 1000000.0) : 0.0;

				uint64_t cur_apds = rx->audio_stats ? rx->audio_stats->total_periods.load() : 0;
				uint64_t cur_abytes = rx->audio_stats ? rx->audio_stats->total_bytes.load() : 0;
				uint64_t cur_asamples = rx->audio_stats ? rx->audio_stats->total_samples.load() : 0;
				uint64_t delta_apds = cur_apds - last_apds;
				last_apds = cur_apds;
				double a_rate = dt_sec > 0 ? (double)delta_apds / dt_sec : 0.0;

				uint32_t underflows = get_uint_prop(rx->rx_dep_src, "underflow-count");
				uint32_t overruns = get_uint_prop(rx->rx_dep_src, "overrun-count");
				uint32_t active_flows = zst_dante_video_coordinator_get_flow_count(rx->coordinator);

				if (active_flows > 0 || cur_vpkts > 0) {
					LOGI("[DANTE RX STATS] flows=%u | VIDEO: %lu pkts (%.2f MB, %.1f fps, %.2f Mbps, keyframes=%lu, IDR=%lu, SPS/PPS=%lu) | AUDIO: %lu periods (%.2f MB, %lu samples, %.1f p/s, underflow=%u, overrun=%u)",
						active_flows,
						(unsigned long)cur_vpkts,
						(double)cur_vbytes / (1024.0 * 1024.0),
						fps, mbps,
						(unsigned long)cur_keyframes,
						(unsigned long)cur_idr,
						(unsigned long)cur_sps_pps,
						(unsigned long)cur_apds,
						(double)cur_abytes / (1024.0 * 1024.0),
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
	                                     uint32_t stat_interval)
	{
		zst_dante_rx_t* rx = new zst_dante_rx_t();
		rx->dvr_socket = dvr_socket;
		rx->dep_name = dep_name;
		rx->channels = channels;
		rx->sample_rate = sample_rate;
		rx->stat_interval_sec = stat_interval > 0 ? stat_interval : 3;

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

		/* 1. Dante DVR Session */
		rx->session = zst_dante_session_create(rx->dvr_socket.c_str());
		if (!rx->session) {
			LOGE("zst_dante_session_create(%s) failed", rx->dvr_socket.c_str());
			zst_pipeline_destroy(rx->pipe);
			delete rx;
			return nullptr;
		}
		set_uint(rx->session, "tx-video-channels", 0);
		set_uint(rx->session, "rx-video-channels", 1);

		/* 2. Dante Video Coordinator */
		rx->coordinator = zst_dante_video_coordinator_create();
		if (!rx->coordinator) {
			LOGE("zst_dante_video_coordinator_create() failed");
			zst_element_destroy(rx->session);
			zst_pipeline_destroy(rx->pipe);
			delete rx;
			return nullptr;
		}
		zst_dante_video_coordinator_attach_session(rx->coordinator, rx->session);
		rx->rx_video_pad = zst_dante_video_coordinator_request_rx_output_pad(rx->coordinator, 0);
		if (!rx->rx_video_pad) {
			LOGE("zst_dante_video_coordinator_request_rx_output_pad() failed");
			zst_element_destroy(rx->coordinator);
			zst_element_destroy(rx->session);
			zst_pipeline_destroy(rx->pipe);
			delete rx;
			return nullptr;
		}

		/* 3. RX Video Sink (packet inspector, no decode) */
		rx->rx_video_sink = create_rx_video_sink(&rx->video_stats);
		if (!rx->rx_video_sink) {
			LOGE("create_rx_video_sink() failed");
			zst_element_destroy(rx->coordinator);
			zst_element_destroy(rx->session);
			zst_pipeline_destroy(rx->pipe);
			delete rx;
			return nullptr;
		}
		zst_pad_link(rx->rx_video_pad, zst_element_get_pad(rx->rx_video_sink, "sink"));

		/* 4. Dante DEP Audio Source */
		rx->rx_dep_src = zst_dante_dep_audio_source_create();
		if (!rx->rx_dep_src) {
			LOGE("zst_dante_dep_audio_source_create() failed");
			zst_element_destroy(rx->rx_video_sink);
			zst_element_destroy(rx->coordinator);
			zst_element_destroy(rx->session);
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
			zst_element_destroy(rx->session);
			zst_pipeline_destroy(rx->pipe);
			delete rx;
			return nullptr;
		}
		zst_pad_link(zst_element_get_pad(rx->rx_dep_src, "src"),
		             zst_element_get_pad(rx->rx_audio_sink, "sink"));

		/* Add elements to pipeline */
		zst_pipeline_add(rx->pipe, rx->coordinator);
		zst_pipeline_add(rx->pipe, rx->rx_video_sink);
		zst_pipeline_add(rx->pipe, rx->rx_dep_src);
		zst_pipeline_add(rx->pipe, rx->rx_audio_sink);

		/* Clocks and pipeline state */
		rx->clock = zst_clock_system_create();
		zst_pipeline_set_clock(rx->pipe, rx->clock);
		zst_pipeline_set_state(rx->pipe, ZST_STATE_PLAYING);
		rx->session->bus = rx->pipe->bus;
		if (zst_element_set_state(rx->session, ZST_STATE_PLAYING) != ZST_OK) {
			LOGW("Dante session start failed (check if /var/run/dante/dvr is active)");
		}

		/* Multi-threaded scheduler */
		zst_scheduler_config_t cfg = {
			.mode = ZST_SCHEDULER_MULTI_THREAD,
			.worker_threads = 4
		};
		rx->sched = zst_scheduler_create(&cfg);
		zst_scheduler_attach(rx->sched, rx->pipe);
		pthread_create(&rx->sched_th, nullptr, (void* (*)(void*))zst_scheduler_run, rx->sched);

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

		/* Snapshot statistics before elements are destroyed */
		uint64_t vpkts = rx->video_stats ? rx->video_stats->total_packets.load() : 0;
		uint64_t vbytes = rx->video_stats ? rx->video_stats->total_bytes.load() : 0;
		uint64_t keyframes = rx->video_stats ? rx->video_stats->keyframes.load() : 0;
		uint64_t idr = rx->video_stats ? rx->video_stats->idr_slices.load() : 0;
		uint64_t sps_pps = rx->video_stats ? rx->video_stats->sps_pps.load() : 0;
		uint64_t apds = rx->audio_stats ? rx->audio_stats->total_periods.load() : 0;
		uint64_t asamples = rx->audio_stats ? rx->audio_stats->total_samples.load() : 0;
		uint64_t abytes = rx->audio_stats ? rx->audio_stats->total_bytes.load() : 0;

		if (rx->session) {
			zst_element_set_state(rx->session, ZST_STATE_NULL);
			zst_element_destroy(rx->session);
			rx->session = nullptr;
		}
		if (rx->sched) {
			zst_scheduler_stop(rx->sched);
			pthread_join(rx->sched_th, nullptr);
			zst_scheduler_destroy(rx->sched);
			rx->sched = nullptr;
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
		printf("  Audio Periods:       %llu\n", (unsigned long long)apds);
		printf("  Audio Samples:       %llu (%.2f MB)\n",
		       (unsigned long long)asamples,
		       (double)abytes / (1024.0 * 1024.0));
		printf("========================================================\n\n");

		delete rx;
		LOGI("Dante RX pipeline stopped.");
	}

	static volatile sig_atomic_t g_stop_requested = 0;
	static void sig_handler(int sig) {
		(void)sig;
		g_stop_requested = 1;
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

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
			printf("Usage: sc6f0-dante-rx [OPTIONS]\n");
			printf("Options:\n");
			printf("  --dvr <path>       Path to Dante DVR socket (default: /var/run/dante/dvr)\n");
			printf("  --dep <name>       Dante DEP shared memory name (default: DanteEP)\n");
			printf("  --channels <list>  Audio channels to monitor (default: 0,1)\n");
			printf("  --rate <hz>        Audio sample rate in Hz (default: 48000)\n");
			printf("  --interval <sec>   Periodic statistics interval (default: 3)\n");
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
		}
	}

	signal(SIGINT, sig_handler);
	signal(SIGTERM, sig_handler);

	printf("============================================================\n");
	printf(" SC6F0 Dante RX (zstreamer pipeline, non-decode mode)\n");
	printf(" DVR Socket:    %s\n", dvr_socket.c_str());
	printf(" DEP SharedMem: %s\n", dep_name.c_str());
	printf(" Channels:      %s\n", channels.c_str());
	printf(" Sample Rate:   %u Hz\n", sample_rate);
	printf(" Stat Interval: %u s\n", stat_interval);
	printf(" Press 'q' + Enter (or Ctrl+C) to stop.\n");
	printf("============================================================\n\n");

	zst_dante_rx_t* rx = start_dante_rx(dvr_socket, dep_name, channels, sample_rate, stat_interval);
	if (!rx) {
		LOGE("Failed to start Dante RX pipeline.");
		return 1;
	}

	__testkit__::wait_for_test_finish([&](int ch) -> bool {
		if (ch == 'q' || ch == 'Q' || g_stop_requested) {
			return false;
		}
		return true;
	}, 1000000LL, 10LL);

	stop_dante_rx(rx);
	return 0;
}
