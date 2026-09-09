#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "qcap2.h"
#include "qcap2.user.h"

#include "ZzLog.h"
#include "ZzModules.h"

ZZ_INIT_LOG("sc6f0-allegro2-encode-fixture");

int g_argc = 0;
char** g_argv = NULL;
ZZ_MODULE_DECL(__zz_log__);

namespace {
	ZZ_MODULES_INIT();

	struct modules_init_t {
		modules_init_t() { ZZ_MODULE_INIT(__zz_log__); }
		~modules_init_t() { ZZ_MODULES_UNINIT(); }
	};

	static int drain_encoder(qcap2_video_encoder_t* encoder, FILE* output,
	                         int* produced)
	{
		for (;;) {
			qcap2_rcbuffer_t* buffer = NULL;
			QRESULT result = qcap2_video_encoder_pop(encoder, &buffer);
			if (result == QCAP_RS_ERROR_TIMEOUT) return 0;
			if (result != QCAP_RS_SUCCESSFUL) {
				LOGE("qcap2_video_encoder_pop() failed, qres=%d", result);
				return -1;
			}

			qcap2_av_packet_t* packet =
				(qcap2_av_packet_t*)qcap2_rcbuffer_lock_data(buffer);
			uint8_t* data = NULL;
			int size = 0;
			qcap2_av_packet_get_buffer(packet, &data, &size);
			qcap2_rcbuffer_unlock_data(buffer);
			if (!data || size <= 0 || fwrite(data, 1, (size_t)size, output) != (size_t)size) {
				LOGE("failed to write encoded access unit");
				qcap2_rcbuffer_release(buffer);
				return -1;
			}
			qcap2_rcbuffer_release(buffer);
			(*produced)++;
		}
	}
}

int main(int argc, char* argv[])
{
	modules_init_t modules;
	g_argc = argc;
	g_argv = argv;

	const char* output_path = argc > 1 ? argv[1] : "allegro2-fixture.h264";
	int frame_count = argc > 2 ? atoi(argv[2]) : 120;
	int slices = argc > 3 ? atoi(argv[3]) : 4;
	if (frame_count < 2 || slices < 1) {
		fprintf(stderr, "Usage: %s [output.h264] [frames>=2] [slices>=1]\n", argv[0]);
		return 2;
	}

	const ULONG width = 3840;
	const ULONG height = 2160;
	const double fps = 60.0;
	QRESULT result = QCAP_RS_SUCCESSFUL;
	int produced = 0;
	FILE* output = fopen(output_path, "wb");
	if (!output) {
		perror(output_path);
		return 1;
	}

	qcap2_frame_pool_t* pool = qcap2_frame_pool_new();
	qcap2_video_encoder_t* encoder = qcap2_video_encoder_new();
	qcap2_video_encoder_property_t* property = qcap2_video_encoder_property_new();
	if (!pool || !encoder || !property) {
		LOGE("failed to allocate qcap encoder objects");
		result = QCAP_RS_ERROR_OUT_OF_MEMORY;
		goto done;
	}

	qcap2_frame_pool_set_backend_type(pool, QCAP2_FRAME_POOL_BACKEND_TYPE_QDMABUF);
	qcap2_frame_pool_set_frame_count(pool, 4);
	qcap2_frame_pool_set_video_frame_align(pool, 32, 32);
	qcap2_frame_pool_set_video_property(pool, QCAP_COLORSPACE_TYPE_NV12, width, height);
	qcap2_frame_pool_set_video_property1(pool, 0, 0, true);
	if ((result = qcap2_frame_pool_start(pool)) != QCAP_RS_SUCCESSFUL) {
		LOGE("qcap2_frame_pool_start() failed, qres=%d", result);
		goto done;
	}

	qcap2_video_encoder_property_set_property1(
		property, 0, QCAP_ENCODER_TYPE_ALLEGRO2, QCAP_ENCODER_FORMAT_H264,
		QCAP_COLORSPACE_TYPE_NV12, width, height, fps,
		QCAP_RECORD_PROFILE_HIGH, QCAP_RECORD_LEVEL_52, QCAP_RECORD_ENTROPY_CABAC,
		QCAP_RECORD_COMPLEXITY_0, QCAP_RECORD_MODE_CBR, 8000, 40000000,
		60, 0, FALSE, (ULONG)slices, 0, 0, FALSE, FALSE, FALSE,
		0, 0, 0, 0, 0, 0);
	qcap2_video_encoder_property_set_low_delay(property, true);
	qcap2_video_encoder_property_set_time_scale_factor(property, 1);
	qcap2_video_encoder_set_video_property(encoder, property);
	qcap2_video_encoder_set_multithread(encoder, false);
	qcap2_video_encoder_set_num_cores(encoder, 4);
	if ((result = qcap2_video_encoder_start(encoder)) != QCAP_RS_SUCCESSFUL) {
		LOGE("qcap2_video_encoder_start() failed, qres=%d", result);
		goto done;
	}

	LOGI("Encoding %d NV12 QDMA test-pattern frames: %lux%lu @ %.1f, GOP=60, slices=%d",
	     frame_count, width, height, fps, slices);
	for (int frame = 0; frame < frame_count; frame++) {
		qcap2_rcbuffer_t* input = NULL;
		for (int wait_ms = 0;; wait_ms++) {
			result = qcap2_frame_pool_get_buffer(pool, &input);
			if (result == QCAP_RS_SUCCESSFUL) break;
			if (result != QCAP_RS_ERROR_TIMEOUT || wait_ms >= 5000) {
				LOGE("qcap2_frame_pool_get_buffer() failed at frame %d, qres=%d", frame, result);
				goto done;
			}
			if (drain_encoder(encoder, output, &produced) != 0) {
				result = QCAP_RS_ERROR_GENERAL;
				goto done;
			}
			usleep(1000);
		}
		result = qcap2_fill_video_test_pattern(input, 0);
		if (result == QCAP_RS_SUCCESSFUL) {
			qcap2_av_frame_t* avframe =
				(qcap2_av_frame_t*)qcap2_rcbuffer_lock_data(input);
			qcap2_av_frame_set_sample_time(avframe, (double)frame / fps);
			qcap2_rcbuffer_unlock_data(input);
			result = qcap2_video_encoder_push(encoder, input);
		}
		qcap2_rcbuffer_release(input);
		if (result != QCAP_RS_SUCCESSFUL) {
			LOGE("encoder push failed at frame %d, qres=%d", frame, result);
			goto done;
		}
		if (drain_encoder(encoder, output, &produced) != 0) {
			result = QCAP_RS_ERROR_GENERAL;
			goto done;
		}
		usleep(1000000 / (unsigned int)fps);
	}
	if ((result = qcap2_video_encoder_push(encoder, NULL)) != QCAP_RS_SUCCESSFUL) {
		LOGE("encoder flush failed, qres=%d", result);
		goto done;
	}
	for (int wait_ms = 0; produced < frame_count && wait_ms < 5000; wait_ms++) {
		if (drain_encoder(encoder, output, &produced) != 0) {
			result = QCAP_RS_ERROR_GENERAL;
			goto done;
		}
		usleep(1000);
	}
	if (produced != frame_count) {
		LOGE("encoder produced %d/%d access units", produced, frame_count);
		result = QCAP_RS_ERROR_TIMEOUT;
		goto done;
	}
	if (fflush(output) != 0) {
		perror("fflush");
		result = QCAP_RS_ERROR_GENERAL;
		goto done;
	}
	LOGI("Wrote %d Annex-B H.264 access units to %s", produced, output_path);
	usleep(100000);

done:
	if (encoder) {
		LOGI("Stopping ALLEGRO2 encoder...");
		qcap2_video_encoder_stop(encoder);
		LOGI("ALLEGRO2 encoder stopped");
	}
	if (pool) {
		LOGI("Stopping QDMA frame pool...");
		qcap2_frame_pool_stop(pool);
		LOGI("QDMA frame pool stopped");
	}
	if (property) qcap2_video_encoder_property_delete(property);
	if (encoder) qcap2_video_encoder_delete(encoder);
	if (pool) qcap2_frame_pool_delete(pool);
	fclose(output);
	return result == QCAP_RS_SUCCESSFUL ? 0 : 1;
}
