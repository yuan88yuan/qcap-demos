#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <fstream>
#include <vector>

#include "qcap2.h"
#include "qcap2.user.h"
#include "qcap.common.h"

#include "ZzLog.h"
#include "ZzModules.h"

ZZ_INIT_LOG("sc6f0-allegro2-decode-fixture");

int g_argc = 0;
char** g_argv = NULL;
ZZ_MODULE_DECL(__zz_log__);

namespace {
	ZZ_MODULES_INIT();

	struct modules_init_t {
		modules_init_t() { ZZ_MODULE_INIT(__zz_log__); }
		~modules_init_t() { ZZ_MODULES_UNINIT(); }
	};

	struct nal_t {
		size_t start;
		size_t payload;
		size_t end;
		uint8_t type;
	};

	static std::vector<nal_t> scan_annexb(const std::vector<uint8_t>& data)
	{
		std::vector<nal_t> nals;
		for (size_t i = 0; i + 3 < data.size();) {
			size_t start_code = 0;
			if (data[i] == 0 && data[i + 1] == 0) {
				if (data[i + 2] == 1) start_code = 3;
				else if (i + 4 <= data.size() && data[i + 2] == 0 && data[i + 3] == 1)
					start_code = 4;
			}
			if (!start_code) {
				++i;
				continue;
			}
			size_t payload = i + start_code;
			if (payload < data.size())
				nals.push_back({i, payload, 0, (uint8_t)(data[payload] & 0x1f)});
			i = payload;
		}
		for (size_t i = 0; i < nals.size(); ++i)
			nals[i].end = i + 1 < nals.size() ? nals[i + 1].start : data.size();
		return nals;
	}

	static bool make_avcc(const std::vector<uint8_t>& data, const std::vector<nal_t>& nals,
	                      std::vector<uint8_t>* avcc)
	{
		const nal_t* sps = NULL;
		const nal_t* pps = NULL;
		for (const nal_t& nal : nals) {
			if (nal.type == 7 && !sps) sps = &nal;
			if (nal.type == 8 && !pps) pps = &nal;
		}
		if (!sps || !pps || sps->end - sps->payload < 4 || pps->end - pps->payload < 1 ||
		    sps->end - sps->payload > UINT16_MAX || pps->end - pps->payload > UINT16_MAX)
			return false;

		size_t sps_size = sps->end - sps->payload;
		size_t pps_size = pps->end - pps->payload;
		avcc->resize(11 + sps_size + pps_size);
		uint8_t* out = avcc->data();
		out[0] = 1;
		out[1] = data[sps->payload + 1];
		out[2] = data[sps->payload + 2];
		out[3] = data[sps->payload + 3];
		out[4] = 0xff;
		out[5] = 0xe1;
		out[6] = (uint8_t)(sps_size >> 8);
		out[7] = (uint8_t)sps_size;
		memcpy(out + 8, data.data() + sps->payload, sps_size);
		size_t pps_offset = 8 + sps_size;
		out[pps_offset] = 1;
		out[pps_offset + 1] = (uint8_t)(pps_size >> 8);
		out[pps_offset + 2] = (uint8_t)pps_size;
		memcpy(out + pps_offset + 3, data.data() + pps->payload, pps_size);
		return true;
	}

	static int drain_decoder(qcap2_video_decoder_t* decoder, int* decoded)
	{
		for (;;) {
			qcap2_rcbuffer_t* frame = NULL;
			QRESULT result = qcap2_video_decoder_pop(decoder, &frame);
			if (result == QCAP_RS_ERROR_TIMEOUT) return 0;
			if (result != QCAP_RS_SUCCESSFUL) {
				LOGE("qcap2_video_decoder_pop() failed, qres=%d", result);
				return -1;
			}
			qcap2_rcbuffer_release(frame);
			(*decoded)++;
		}
	}

	static int push_au(qcap2_video_decoder_t* decoder, qcap2_packet_pool_t* packet_pool,
		               const uint8_t* data, size_t size, int64_t pts)
	{
		QRESULT result;

		qcap2_rcbuffer_t* buffer = NULL;
		result = qcap2_packet_pool_get_buffer(packet_pool, size, &buffer);
		if (!buffer) return -1;
		qcap2_av_packet_t* packet =
			(qcap2_av_packet_t*)qcap2_rcbuffer_lock_data(buffer);
		uint8_t* dst;
		int dst_size;
		qcap2_av_packet_get_buffer(packet, &dst, &dst_size);
		memcpy(dst, data, size);
		qcap2_av_packet_set_property(packet, 0, TRUE);
		qcap2_av_packet_set_pts(packet, pts);
		qcap2_av_packet_set_dts(packet, pts);
		qcap2_rcbuffer_unlock_data(buffer);
		result = qcap2_video_decoder_push(decoder, buffer);
		qcap2_rcbuffer_release(buffer);
		return result == QCAP_RS_SUCCESSFUL ? 0 : -1;
	}
}

int main(int argc, char* argv[])
{
	modules_init_t modules;
	g_argc = argc;
	g_argv = argv;
	if (argc < 2) {
		fprintf(stderr, "Usage: %s input.h264 [width=3840] [height=2160] [fps=60]\n", argv[0]);
		return 2;
	}

	ULONG width = argc > 2 ? (ULONG)strtoul(argv[2], NULL, 10) : 3840;
	ULONG height = argc > 3 ? (ULONG)strtoul(argv[3], NULL, 10) : 2160;
	double fps = argc > 4 ? strtod(argv[4], NULL) : 60.0;
	std::ifstream input(argv[1], std::ios::binary | std::ios::ate);
	if (!input) {
		perror(argv[1]);
		return 1;
	}
	size_t input_size = (size_t)input.tellg();
	std::vector<uint8_t> bitstream(input_size);
	input.seekg(0);
	input.read((char*)bitstream.data(), (std::streamsize)bitstream.size());
	if (!input || bitstream.empty()) {
		LOGE("failed to read %s", argv[1]);
		return 1;
	}

	std::vector<nal_t> nals = scan_annexb(bitstream);
	std::vector<uint8_t> avcc;
	if (!make_avcc(bitstream, nals, &avcc)) {
		LOGE("%s does not contain valid Annex-B SPS/PPS", argv[1]);
		return 1;
	}

	std::vector<size_t> au_starts;
	for (const nal_t& nal : nals)
		if (nal.type == 9) au_starts.push_back(nal.start);
	if (au_starts.empty()) au_starts.push_back(0);
	if (au_starts.front() != 0) au_starts.insert(au_starts.begin(), 0);

	qcap2_packet_pool_t* packet_pool = qcap2_packet_pool_new();
	qcap2_packet_pool_set_packet_count(packet_pool, 16);
	if (!packet_pool) {
		LOGE("failed to allocate packet pool");
		return 1;
	}

	QRESULT result = qcap2_packet_pool_start(packet_pool);
	if (result != QCAP_RS_SUCCESSFUL) {
		LOGE("qcap2_video_decoder_start() failed, qres=%d", result);
		return 1;
	}

	qcap2_video_decoder_t* decoder = qcap2_video_decoder_new();
	qcap2_video_encoder_property_t* property = qcap2_video_encoder_property_new();
	if (!decoder || !property) {
		LOGE("failed to allocate qcap decoder objects");
		return 1;
	}
	qcap2_video_encoder_property_set_property(property, QCAP_ENCODER_TYPE_ALLEGRO2,
		QCAP_ENCODER_FORMAT_H264, QCAP_COLORSPACE_TYPE_NV12, width, height, fps,
		0, 0, 0, 0, 0, 0);
	qcap2_video_decoder_set_video_property(decoder, property);
	qcap2_video_decoder_set_extra_data(decoder, avcc.data(), (int)avcc.size());
	qcap2_video_decoder_set_frame_count(decoder, 8);
	qcap2_video_decoder_set_packet_count(decoder, 16);
	qcap2_video_decoder_set_max_packet_size(decoder, 1024 * 1024);
	result = qcap2_video_decoder_start(decoder);
	if (result != QCAP_RS_SUCCESSFUL) {
		LOGE("qcap2_video_decoder_start() failed, qres=%d", result);
		qcap2_video_encoder_property_delete(property);
		qcap2_video_decoder_delete(decoder);
		return 1;
	}

	LOGI("Decoding %zu AU(s) from %s: %lux%lu @ %.1f fps, avcC=%zu bytes",
	     au_starts.size(), argv[1], width, height, fps, avcc.size());
	int decoded = 0;
	int pushed = 0;
	for (size_t i = 0; i < au_starts.size(); ++i) {
		size_t end = i + 1 < au_starts.size() ? au_starts[i + 1] : bitstream.size();
		if (push_au(decoder, packet_pool, bitstream.data() + au_starts[i], end - au_starts[i],
		            (int64_t)i * 1000000 / (int64_t)fps) != 0) {
			LOGE("decoder push failed at AU %zu", i);
			break;
		}
		pushed++;
		if (drain_decoder(decoder, &decoded) != 0) break;
	}
	(void)qcap2_video_decoder_push(decoder, NULL);
	for (int wait_ms = 0; wait_ms < 3000; wait_ms++) {
		if (drain_decoder(decoder, &decoded) != 0) break;
		usleep(1000);
	}
	LOGI("Decoder result: pushed=%d decoded=%d", pushed, decoded);
	qcap2_video_decoder_stop(decoder);
	qcap2_video_encoder_property_delete(property);
	qcap2_video_decoder_delete(decoder);
	qcap2_packet_pool_stop(packet_pool);
	qcap2_packet_pool_delete(packet_pool);
	return pushed == (int)au_starts.size() && decoded > 0 ? 0 : 1;
}
