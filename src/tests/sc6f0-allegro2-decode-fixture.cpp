#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <atomic>
#include <functional>
#include <memory>

#include <fstream>
#include <vector>

#include "qcap2.h"
#include "qcap2.allegro2.h"
#include "qcap2.user.h"
#include "qcap.common.h"

#include "ZzLog.h"
#include "ZzModules.h"
#include "ZzClock.h"
#include "ZzUtils.h"
#include "testkit.h"

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
}

// ==========================================================================
// Allegro2DecodeFixture
//
//  Reads an H.264 Annex-B bitstream, converts SPS/PPS to avcC, then feeds
//  AU-by-AU into an Allegro2 video decoder using the qcap2_event_handlers_t
//  async mechanism (timer-driven push + event-driven pop).
//
//  Push timeout (decoder queue full): on QCAP_RS_ERROR_TIMEOUT the packet
//  index is decremented so the same AU is retried on the next timer tick
//  after the decoder has freed a buffer.
// ==========================================================================
struct Allegro2DecodeFixture : public __testkit__::TestCase {
	typedef Allegro2DecodeFixture self_t;
	typedef __testkit__::TestCase super_t;
	typedef __testkit__::free_stack_t free_stack_t;

	const char*        mInputPath;
	ULONG              mWidth;
	ULONG              mHeight;
	double             mFps;

	// Parsed input
	std::vector<uint8_t>      mFileData;
	std::vector<size_t>       mAustarts;
	std::vector<size_t>       mAusize;
	std::vector<uint8_t>      mAvcc;

	// Decoder / async event loop / input packet pool
	qcap2_video_decoder_t*    mpVdec;
	qcap2_event_t*            mpVdecEvent;
	qcap2_timer_t*            mpTimer;
	qcap2_packet_pool_t*      mpPacketPool;

	// Runtime counters
	int                       mPushed;
	int                       mPopCount;
	bool                      mAllPushed;
	bool                      mFlushSent;
	bool                      mFailed;
	bool                      mDone;
	int64_t                   mDrainDeadline;

	explicit Allegro2DecodeFixture()
		: mInputPath(NULL)
		, mpVdec(NULL), mpVdecEvent(NULL), mpTimer(NULL), mpPacketPool(NULL)
		, mPushed(0), mPopCount(0), mAllPushed(false), mFlushSent(false)
		, mFailed(false), mDone(false), mDrainDeadline(0)
	{
	}

	// -----------------------------------------------------------------------
	bool Load(const char* path, ULONG width, ULONG height, double fps)
	{
		mInputPath = path;
		mWidth     = width;
		mHeight    = height;
		mFps       = fps;

		std::ifstream ifs(path, std::ios::binary | std::ios::ate);
		if (!ifs) { perror(path); return false; }
		size_t sz = (size_t)ifs.tellg();
		ifs.seekg(0);
		mFileData.resize(sz);
		ifs.read((char*)mFileData.data(), (std::streamsize)sz);
		if (!ifs || mFileData.empty()) {
			LOGE("failed to read %s", path);
			return false;
		}

		std::vector<nal_t> nals = scan_annexb(mFileData);
		std::vector<uint8_t> avcc;
		if (!make_avcc(mFileData, nals, &avcc)) {
			LOGE("%s does not contain valid Annex-B SPS/PPS", path);
			return false;
		}

		mAustarts.clear();
		mAusize.clear();
		for (size_t i = 0; i < nals.size(); ++i) {
			if (nals[i].type == 9) mAustarts.push_back(nals[i].start);
		}
		if (mAustarts.empty()) mAustarts.push_back(0);
		if (mAustarts.front() != 0) {
			mAustarts.insert(mAustarts.begin(), 0);
		}
		for (size_t i = 0; i < mAustarts.size(); ++i) {
			size_t end = i + 1 < mAustarts.size() ? mAustarts[i + 1] : mFileData.size();
			mAusize.push_back(end - mAustarts[i]);
		}

		mAvcc = std::move(avcc);
		return true;
	}

	// -----------------------------------------------------------------------
	QRESULT Setup(free_stack_t& _FreeStack_, qcap2_event_handlers_t* pEventHandlers)
	{
		QRESULT qres;
		mPushed = 0;
		mPopCount = 0;
		mAllPushed = false;
		mFlushSent = false;
		mFailed = false;
		mDone = false;
		mDrainDeadline = 0;

		switch(1) { case 1:
			// ---- decoder event ----
			qres = __testkit__::NewEvent(_FreeStack_, &mpVdecEvent);
			if (qres != QCAP_RS_SUCCESSFUL) {
				LOGE("NewEvent() failed, qres=%d", qres);
				break;
			}

			// ---- timer ----
			mpTimer = qcap2_timer_new();
			_FreeStack_ += [this]() { qcap2_timer_delete(mpTimer); };
			qres = qcap2_timer_start(mpTimer);
			if (qres != QCAP_RS_SUCCESSFUL) {
				LOGE("qcap2_timer_start() failed, qres=%d", qres);
				break;
			}
			_FreeStack_ += [this]() {
				QRESULT qr = qcap2_timer_stop(mpTimer);
				if (qr != QCAP_RS_SUCCESSFUL)
					LOGE("qcap2_timer_stop() failed, qres=%d", qr);
			};

			// ---- decoder ----
			mpVdec = qcap2_video_decoder_new();
			_FreeStack_ += [this]() { qcap2_video_decoder_delete(mpVdec); };

			{
				std::shared_ptr<qcap2_video_encoder_property_t> pProp(
					qcap2_video_encoder_property_new(),
					qcap2_video_encoder_property_delete);
				qcap2_video_encoder_property_set_property(pProp.get(),
					QCAP_ENCODER_TYPE_ALLEGRO2,
					QCAP_ENCODER_FORMAT_H264,
					QCAP_COLORSPACE_TYPE_NV12,
					mWidth, mHeight, mFps,
					0, 0, 0, 0, 0, 0);
				qcap2_video_decoder_set_video_property(mpVdec, pProp.get());
			}

			qcap2_video_decoder_set_event(mpVdec, mpVdecEvent);
			qcap2_video_decoder_set_extra_data(mpVdec, mAvcc.data(), (int)mAvcc.size());
			// Each input buffer contains one complete Annex-B access unit.
			qcap2_video_decoder_set_input_mode(mpVdec, AL_DEC_SPLIT_INPUT);
			qcap2_video_decoder_set_multithread(mpVdec, false);
			qcap2_video_decoder_set_frame_count(mpVdec, 8);
			qcap2_video_decoder_set_packet_count(mpVdec, 16);
			qcap2_video_decoder_set_max_packet_size(mpVdec, 1024 * 1024);

			// ---- packet pool ----
			mpPacketPool = qcap2_packet_pool_new();
			if (!mpPacketPool) {
				LOGE("qcap2_packet_pool_new() failed");
				qres = QCAP_RS_ERROR_OUT_OF_MEMORY;
				break;
			}
			_FreeStack_ += [this]() {
				qcap2_packet_pool_delete(mpPacketPool);
				mpPacketPool = NULL;
			};
			qcap2_packet_pool_set_packet_count(mpPacketPool, 16);
			qres = qcap2_packet_pool_start(mpPacketPool);
			if (qres != QCAP_RS_SUCCESSFUL) {
				LOGE("qcap2_packet_pool_start() failed, qres=%d", qres);
				break;
			}
			_FreeStack_ += [this]() {
				QRESULT qr = qcap2_packet_pool_stop(mpPacketPool);
				if (qr != QCAP_RS_SUCCESSFUL)
					LOGE("qcap2_packet_pool_stop() failed, qres=%d", qr);
			};

			qres = qcap2_video_decoder_start(mpVdec);
			if (qres != QCAP_RS_SUCCESSFUL) {
				LOGE("qcap2_video_decoder_start() failed, qres=%d", qres);
				break;
			}
			_FreeStack_ += [this]() {
				QRESULT qr = qcap2_video_decoder_stop(mpVdec);
				if (qr != QCAP_RS_SUCCESSFUL)
					LOGE("qcap2_video_decoder_stop() failed, qres=%d", qr);
			};

			// ---- register callbacks ----
			qres = __testkit__::AddTimerHandler(_FreeStack_, pEventHandlers, mpTimer,
				std::bind(&self_t::OnTimer, this));
			if (qres != QCAP_RS_SUCCESSFUL) {
				LOGE("AddTimerHandler() failed, qres=%d", qres);
				break;
			}

			qres = __testkit__::AddEventHandler(_FreeStack_, pEventHandlers, mpVdecEvent,
				std::bind(&self_t::OnVdecEvent, this));
			if (qres != QCAP_RS_SUCCESSFUL) {
				LOGE("AddEventHandler(vdec) failed, qres=%d", qres);
				break;
			}

			// ---- pace input at the stream frame rate ----
			qres = qcap2_timer_next(mpTimer, 1000000 / (uint64_t)mFps);
			if (qres != QCAP_RS_SUCCESSFUL) {
				LOGE("qcap2_timer_next() failed, qres=%d", qres);
				break;
			}
		}

		return qres;
	}

	// -----------------------------------------------------------------------
	void WaitForCompletion()
	{
		LOGI("Async decoding ...");
		__testkit__::wait_for_test_finish([this](int) -> bool {
			return !mDone;
		}, 1000000LL, 100LL);
	}

	// -----------------------------------------------------------------------
	// Timer callback: push one AU per frame interval so decoded output buffers
	// are released before the hardware decoder exhausts its frame pool.
	// -----------------------------------------------------------------------
	QRETURN OnTimer()
	{
		QRESULT qres;

		switch(1) { case 1:
			uint64_t nExpirations;
			qres = qcap2_timer_wait(mpTimer, &nExpirations);
			if (qres != QCAP_RS_SUCCESSFUL) break;

			qres = qcap2_timer_next(mpTimer, 1000000 / (uint64_t)mFps);
			if (qres != QCAP_RS_SUCCESSFUL) break;

			// Push phase: submit one AU and retry it on the next tick if full.
			if (!mAllPushed) {
				if (mPushed < (int)mAustarts.size()) {
					int nSize = (int)mAusize[mPushed];
					qcap2_rcbuffer_t* pBuf = NULL;
					qres = qcap2_packet_pool_get_buffer(mpPacketPool, nSize, &pBuf);
					if (qres != QCAP_RS_SUCCESSFUL) {
						if (qres == QCAP_RS_ERROR_TIMEOUT) {
							return QCAP_RT_OK;
						}
						LOGE("qcap2_packet_pool_get_buffer(%d) failed, qres=%d", nSize, qres);
						mFailed = true;
						mDone = true;
						break;
					}

					qcap2_av_packet_t* pPkt =
						(qcap2_av_packet_t*)qcap2_rcbuffer_lock_data(pBuf);
					uint8_t* dst = NULL;
					int dst_sz = 0;
					if (pPkt)
						qcap2_av_packet_get_buffer(pPkt, &dst, &dst_sz);
					if (!pPkt || !dst || dst_sz < nSize) {
						if (pPkt) qcap2_rcbuffer_unlock_data(pBuf);
						qcap2_rcbuffer_release(pBuf);
						LOGE("packet pool returned invalid buffer for %d-byte AU", nSize);
						mFailed = true;
						mDone = true;
						break;
					}
					memcpy(dst, mFileData.data() + mAustarts[mPushed], nSize);
					qcap2_av_packet_set_buffer(pPkt, dst, nSize);
					qcap2_av_packet_set_property(pPkt, 0, TRUE);
					int64_t pts = (int64_t)mPushed * 1000000 / (int64_t)mFps;
					qcap2_av_packet_set_pts(pPkt, pts);
					qcap2_av_packet_set_dts(pPkt, pts);
					qcap2_rcbuffer_unlock_data(pBuf);

					qres = qcap2_video_decoder_push(mpVdec, pBuf);
					qcap2_rcbuffer_release(pBuf);
					if (qres != QCAP_RS_SUCCESSFUL) {
						if (qres == QCAP_RS_ERROR_TIMEOUT) {
							return QCAP_RT_OK;
						}
						LOGE("qcap2_video_decoder_push() failed, qres=%d", qres);
						mFailed = true;
						mDone = true;
						break;
					}
					mPushed++;
					if (mPushed >= (int)mAustarts.size()) {
						mAllPushed = true;
						LOGI("All %d AUs pushed.", mPushed);
					}
				}
			}

			if (mAllPushed && !mFlushSent) {
				qres = qcap2_video_decoder_push(mpVdec, NULL);
				if (qres != QCAP_RS_SUCCESSFUL) {
					LOGE("decoder flush failed, qres=%d", qres);
					mFailed = true;
					mDone = true;
				} else {
					mFlushSent = true;
					mDrainDeadline = __zz_clock__::_clk() + 5000000;
					LOGI("Decoder EOS sent; draining output.");
				}
			}

			if (mFlushSent && !mDone) {
				if (mPopCount >= (int)mAustarts.size()) {
					mDone = true;
					LOGI("Decoder done: pushed=%d decoded=%d", mPushed, mPopCount);
				} else if (__zz_clock__::_clk() >= mDrainDeadline) {
					LOGE("decoder drain timed out: pushed=%d decoded=%d", mPushed, mPopCount);
					mFailed = true;
					mDone = true;
				}
			}
		}

		return QCAP_RT_OK;
	}

	// -----------------------------------------------------------------------
	// Decoder event callback: drain all available output frames.
	// -----------------------------------------------------------------------
	QRETURN OnVdecEvent()
	{
		QRESULT qres;

		switch(1) { case 1:
			while (true) {
				qcap2_rcbuffer_t* pFrame = NULL;
				qres = qcap2_video_decoder_pop(mpVdec, &pFrame);
				if (qres != QCAP_RS_SUCCESSFUL) {
					if (qres == QCAP_RS_ERROR_TIMEOUT) break;
					LOGE("qcap2_video_decoder_pop() failed, qres=%d", qres);
					break;
				}
				qcap2_rcbuffer_release(pFrame);
				mPopCount++;
			}
		}

		return QCAP_RT_OK;
	}
};

// ==========================================================================
// Application
// ==========================================================================
struct App0 {
	typedef App0 self_t;

	static int Main() {
		self_t app;
		return app.Run();
	}

	App0() {}
	~App0() {}

	int Run() {
		int TEST_CASE;
		ZZ_ENV_INIT(TEST_CASE, 0);
		if (TEST_CASE != 0) {
			DoCase(TEST_CASE);
			return 0;
		}

		if (g_argc < 2) {
			fprintf(stderr, "Usage: %s input.h264 [width=3840] [height=2160] [fps=60]\n",
			        g_argv[0]);
			return 2;
		}

		ULONG width  = g_argc > 2 ? (ULONG)strtoul(g_argv[2], NULL, 10) : 3840;
		ULONG height = g_argc > 3 ? (ULONG)strtoul(g_argv[3], NULL, 10) : 2160;
		double fps   = g_argc > 4 ? strtod(g_argv[4], NULL) : 60.0;

		Allegro2DecodeFixture fixture;
		if (!fixture.Load(g_argv[1], width, height, fps)) {
			return 1;
		}

		LOGI("Decoding %zu AU(s) from %s: %lux%lu @ %.1f fps, avcC=%zu bytes",
		     fixture.mAustarts.size(), g_argv[1], width, height, fps,
		     fixture.mAvcc.size());

		int ret = 0;
		switch(1) { case 1:
			__testkit__::free_stack_t& _FreeStack_ = fixture._FreeStack_main_;

			QRESULT qres = fixture.StartEventHandlers();
			if (qres != QCAP_RS_SUCCESSFUL) {
				LOGE("StartEventHandlers() failed, qres=%d", qres);
				ret = 1;
				break;
			}
			ZzUtils::Scoped ZZ_GUARD_NAME([&]() {
				fixture.OnExitEventHandlers();
			});

			QRESULT qres_evt = QCAP_RS_SUCCESSFUL;
			qres = fixture.ExecInEventHandlers([&fixture, &qres_evt, &_FreeStack_]() -> QRETURN {
				qres_evt = fixture.Setup(_FreeStack_, fixture.pEventHandlers);
				return QCAP_RT_OK;
			});
			if (qres != QCAP_RS_SUCCESSFUL) {
				LOGE("ExecInEventHandlers() failed, qres=%d", qres);
				ret = 1;
				break;
			}
			if (qres_evt != QCAP_RS_SUCCESSFUL) {
				ret = 1;
				break;
			}

			fixture.WaitForCompletion();

			if (fixture.mFailed || fixture.mPushed != (int)fixture.mAustarts.size() ||
				fixture.mPopCount != (int)fixture.mAustarts.size()) {
				ret = 1;
			}
		}

		fixture._FreeStack_main_.flush();
		return ret;
	}

	void DoCase(int) {}
};

int main(int argc, char* argv[])
{
	g_argc = argc;
	g_argv = argv;

	int ret;
	switch(1) { case 1:
		modules_init_t _modules_init_;
		ret = App0::Main();
	}
	return ret;
}
