#include "qcap.linux.h"
#include "qcap2.h"
#include "qcap2.user.h"

#include "ZzClock.h"
#include "ZzLog.h"
#include "ZzModules.h"
#include "testkit.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <vector>

#include <unistd.h>

ZZ_INIT_LOG("example-rcbuffer-custom-payload");
ZZ_MODULE_DECL(__zz_log__);

using __testkit__::AddEventHandler;
using __testkit__::ExecInEventHandlers;
using __testkit__::free_stack_t;
using __testkit__::NewEvent;
using __testkit__::StartEventHandlers;
using __zz_clock__::_clk;

namespace __example_rcbuf_custom_payload__ {
	ZZ_MODULES_INIT();

	struct modules_init_t {
		modules_init_t() {
			ZZ_MODULE_INIT(__zz_log__);
		}

		~modules_init_t() {
			ZZ_MODULES_UNINIT();
		}
	};
}

using namespace __example_rcbuf_custom_payload__;

struct CustomPayload {
	qcap2_av_frame_t av_frame;
	int frame_index;
	int64_t timestamp_us;
	char user_metadata[64];
	void* planes[2];
	int strides[2];

	CustomPayload() : frame_index(-1), timestamp_us(0) {
		qcap2_av_frame_init(&av_frame);
		memset(user_metadata, 0, sizeof(user_metadata));
		memset(planes, 0, sizeof(planes));
		memset(strides, 0, sizeof(strides));
	}

	~CustomPayload() {
		free_planes();
	}

	bool init_nv12_buffers(int width, int height) {
		strides[0] = __testkit__::align(width, 16);
		strides[1] = strides[0];

		if(posix_memalign(&planes[0], 16, (size_t)strides[0] * (size_t)height) != 0) {
			LOGE("%s(%d): posix_memalign() failed for Y plane", __FUNCTION__, __LINE__);
			return false;
		}
		if(posix_memalign(&planes[1], 16, (size_t)strides[1] * (size_t)height / 2U) != 0) {
			LOGE("%s(%d): posix_memalign() failed for UV plane", __FUNCTION__, __LINE__);
			free_planes();
			return false;
		}

		uint8_t* pBuffer[4] = {0};
		int pStride[4] = {0};
		pBuffer[0] = (uint8_t*)planes[0];
		pBuffer[1] = (uint8_t*)planes[1];
		pStride[0] = strides[0];
		pStride[1] = strides[1];
		qcap2_av_frame_set_buffer1(&av_frame, pBuffer, pStride);

		return true;
	}

	void free_planes() {
		for(int i = 0; i < 2; ++i) {
			if(planes[i] != NULL) {
				free(planes[i]);
				planes[i] = NULL;
			}
		}
	}

	static void on_free_callback(PVOID pData) {
		CustomPayload* payload = qcap2_container_of(pData, CustomPayload, av_frame);
		payload->on_free_resource();
	}

	void on_free_resource() {
		uint8_t* pBuffer[4] = {0};
		int pStride[4] = {0};
		qcap2_av_frame_get_buffer1(&av_frame, pBuffer, pStride);

		LOGI("on_free_resource: frame=%d ts=%lld meta='%s' Y=%p UV=%p stride=[%d,%d]",
			frame_index,
			(long long)timestamp_us,
			user_metadata,
			pBuffer[0],
			pBuffer[1],
			pStride[0],
			pStride[1]);

		free_planes();
	}
};

struct App {
	static const int kFrameCount = 4;
	static const int kWidth = 640;
	static const int kHeight = 360;

	free_stack_t free_stack_main;
	free_stack_t free_stack_evt;
	qcap2_event_handlers_t* event_handlers;
	std::atomic<int> processed_count;
	std::vector<CustomPayload*> payloads;
	std::vector<qcap2_rcbuffer_t*> held_refs;
	std::atomic<bool> keep_running;

	App() : event_handlers(NULL), processed_count(0), keep_running(true) {}

	int Run() {
		QRESULT qres = QCAP_RS_SUCCESSFUL;

		switch(1) { case 1:
			qres = StartEventHandlers(free_stack_main, &event_handlers);
			if(qres != QCAP_RS_SUCCESSFUL) {
				LOGE("%s(%d): StartEventHandlers() failed, qres=%d", __FUNCTION__, __LINE__, qres);
				break;
			}

			QRESULT qres_evt = QCAP_RS_SUCCESSFUL;
			qres = ExecInEventHandlers(event_handlers,
				std::bind(&App::OnStart, this, std::ref(free_stack_evt), std::ref(qres_evt)));
			if(qres != QCAP_RS_SUCCESSFUL || qres_evt != QCAP_RS_SUCCESSFUL) {
				LOGE("%s(%d): OnStart() failed, qres=%d qres_evt=%d", __FUNCTION__, __LINE__, qres, qres_evt);
				qres = (qres != QCAP_RS_SUCCESSFUL) ? qres : qres_evt;
				break;
			}

			const int64_t start = _clk();
			while(keep_running.load()) {
				if(processed_count.load() >= kFrameCount) {
					keep_running.store(false);
					break;
				}
				if((_clk() - start) > 3LL * 1000000LL) {
					LOGE("%s(%d): timeout waiting for %d frames", __FUNCTION__, __LINE__, kFrameCount);
					qres = QCAP_RS_TIMEOUT;
					keep_running.store(false);
					break;
				}
				usleep(1000 * 5);
			}

			for(size_t i = 0; i < held_refs.size(); ++i) {
				LOGI("release held ref[%zu]=%p", i, held_refs[i]);
				qcap2_rcbuffer_release(held_refs[i]);
			}
			held_refs.clear();
		}

		free_stack_evt.flush();
		free_stack_main.flush();

		for(size_t i = 0; i < payloads.size(); ++i) {
			delete payloads[i];
		}
		payloads.clear();

		if(qres != QCAP_RS_SUCCESSFUL) {
			return 1;
		}

		LOGI("Success: processed=%d", processed_count.load());
		return 0;
	}

	QRETURN OnStart(free_stack_t& free_stack, QRESULT& qres) {
		switch(1) { case 1:
			qcap2_event_t* event_vsrc;
			qres = NewEvent(free_stack, &event_vsrc);
			if(qres != QCAP_RS_SUCCESSFUL) {
				LOGE("%s(%d): NewEvent() failed, qres=%d", __FUNCTION__, __LINE__, qres);
				break;
			}

			qcap2_video_source_t* vsrc;
			qres = StartVsrc(free_stack, event_vsrc, &vsrc);
			if(qres != QCAP_RS_SUCCESSFUL) {
				LOGE("%s(%d): StartVsrc() failed, qres=%d", __FUNCTION__, __LINE__, qres);
				break;
			}

			qres = AddEventHandler(free_stack, event_handlers, event_vsrc,
				std::bind(&App::OnVsrc, this, vsrc));
			if(qres != QCAP_RS_SUCCESSFUL) {
				LOGE("%s(%d): AddEventHandler() failed, qres=%d", __FUNCTION__, __LINE__, qres);
				break;
			}
		}

		return QCAP_RT_OK;
	}

	QRESULT StartVsrc(free_stack_t& free_stack, qcap2_event_t* event_vsrc, qcap2_video_source_t** ppVsrc) {
		QRESULT qres = QCAP_RS_SUCCESSFUL;
		qcap2_rcbuffer_t** buffers = new qcap2_rcbuffer_t*[kFrameCount];
		free_stack += [buffers]() { delete [] buffers; };

		for(int i = 0; i < kFrameCount; ++i) {
			buffers[i] = NULL;

			CustomPayload* payload = new CustomPayload();
			payloads.push_back(payload);
			payload->frame_index = i;
			payload->timestamp_us = _clk();
			snprintf(payload->user_metadata, sizeof(payload->user_metadata), "payload-%d", i);
			qcap2_av_frame_set_video_property(&payload->av_frame,
				QCAP_COLORSPACE_TYPE_NV12, kWidth, kHeight);
			if(!payload->init_nv12_buffers(kWidth, kHeight)) {
				qres = QCAP_RS_FAILURE;
				break;
			}

			qcap2_rcbuffer_t* rcbuf = qcap2_rcbuffer_new(&payload->av_frame, CustomPayload::on_free_callback);
			if(rcbuf == NULL) {
				LOGE("%s(%d): qcap2_rcbuffer_new() failed", __FUNCTION__, __LINE__);
				qres = QCAP_RS_OUT_OF_MEMORY;
				break;
			}
			free_stack += [rcbuf]() { qcap2_rcbuffer_delete(rcbuf); };
			buffers[i] = rcbuf;

			LOGI("prepared frame=%d ts=%lld meta=%s rcbuf=%p", i, (long long)payload->timestamp_us, payload->user_metadata, rcbuf);
		}
		if(qres != QCAP_RS_SUCCESSFUL)
			return qres;

		qcap2_video_source_t* vsrc = qcap2_video_source_new();
		if(vsrc == NULL)
			return QCAP_RS_OUT_OF_MEMORY;
		free_stack += [vsrc]() { qcap2_video_source_delete(vsrc); };

		qcap2_video_source_set_backend_type(vsrc, QCAP2_VIDEO_SOURCE_BACKEND_TYPE_TPG);
		qcap2_video_source_set_frame_count(vsrc, kFrameCount);
		qcap2_video_source_set_buffers(vsrc, &buffers[0]);
		qcap2_video_source_set_event(vsrc, event_vsrc);

		std::shared_ptr<qcap2_video_format_t> video_fmt(qcap2_video_format_new(), qcap2_video_format_delete);
		qcap2_video_format_set_property(video_fmt.get(), QCAP_COLORSPACE_TYPE_NV12, kWidth, kHeight, FALSE, 1.0);
		qcap2_video_source_set_video_format(vsrc, video_fmt.get());

		qres = qcap2_video_source_start(vsrc);
		if(qres != QCAP_RS_SUCCESSFUL) {
			LOGE("%s(%d): qcap2_video_source_start() failed, qres=%d", __FUNCTION__, __LINE__, qres);
			return qres;
		}
		free_stack += [vsrc]() {
			QRESULT qres_stop = qcap2_video_source_stop(vsrc);
			if(qres_stop != QCAP_RS_SUCCESSFUL) {
				LOGE("%s(%d): qcap2_video_source_stop() failed, qres=%d", __FUNCTION__, __LINE__, qres_stop);
			}
		};

		*ppVsrc = vsrc;
		return qres;
	}

	QRETURN OnVsrc(qcap2_video_source_t* vsrc) {
		if(!keep_running.load())
			return QCAP_RT_OK;

		qcap2_rcbuffer_t* rcbuf = NULL;
		QRESULT qres = qcap2_video_source_pop(vsrc, &rcbuf);
		if(qres != QCAP_RS_SUCCESSFUL) {
			LOGE("%s(%d): qcap2_video_source_pop() failed, qres=%d", __FUNCTION__, __LINE__, qres);
			return QCAP_RT_OK;
		}

		std::shared_ptr<qcap2_rcbuffer_t> rcbuf_guard(rcbuf, qcap2_rcbuffer_release);
		qcap2_av_frame_t* av_frame = (qcap2_av_frame_t*)qcap2_rcbuffer_lock_data(rcbuf);
		CustomPayload* payload = qcap2_container_of(av_frame, CustomPayload, av_frame);

		uint8_t* pBuffer[4] = {0};
		int pStride[4] = {0};
		qcap2_av_frame_get_buffer1(av_frame, pBuffer, pStride);

		const int current = processed_count.fetch_add(1) + 1;
		LOGI("OnVsrc[%d/%d]: frame=%d ts=%lld meta=%s Y=%p UV=%p stride=[%d,%d]",
			current,
			kFrameCount,
			payload->frame_index,
			(long long)payload->timestamp_us,
			payload->user_metadata,
			pBuffer[0],
			pBuffer[1],
			pStride[0],
			pStride[1]);

		if(current == 1) {
			qcap2_rcbuffer_add_ref(rcbuf);
			held_refs.push_back(rcbuf);
			LOGI("extra ref handoff for frame=%d rcbuf=%p", payload->frame_index, rcbuf);
		}

		qcap2_rcbuffer_unlock_data(rcbuf);
		if(current >= kFrameCount) {
			keep_running.store(false);
		}

		return QCAP_RT_OK;
	}
};

int main() {
	modules_init_t modules_init;
	App app;
	return app.Run();
}
