#include <stdlib.h>
#include <time.h>
#include <fstream>

#include "qcap2.h"
#include "qcap2.user.h"
#include "qcap2.v4l2.h"

#include "ZzLog.h"
#include "ZzModules.h"
#include "ZzClock.h"
#include "ZzStats.h"
#include "ZzUtils.h"
#include "testkit.h"

ZZ_INIT_LOG("test-vdec-bench");

int g_argc = 0;
char** g_argv = NULL;

ZZ_MODULE_DECL(__zz_log__);

using namespace __zz_clock__;
using __testkit__::wait_for_test_finish;
using __testkit__::TestCase;
using __testkit__::free_stack_t;
using __testkit__::tick_ctrl_t;
using __testkit__::NewEvent;
using __testkit__::AddEventHandler;

namespace __test_vdec_bench__ {
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
}

using namespace __test_vdec_bench__;

// ==========================================================================
// VideoDecoderBenchmark
//
//  Reads an H.264 raw file, feeds NAL units through a qcap2_video_decoder_t
//  configured with a given encoder type, and measures decode throughput.
//
//  Usage:
//    1. Call LoadAndParse() to read the file and locate NAL boundaries.
//    2. Call Setup() from inside an event-handler context (OnStart) to
//       create the decoder, timer, and event registrations.
//    3. Outside the event-handler invoke, call WaitForCompletion() to run
//    4. Call PrintResults() to emit the report.
// ==========================================================================
struct VideoDecoderBenchmark {
	typedef VideoDecoderBenchmark self_t;

	// Input state (must be set before calling LoadAndParse)
	const char*        mSamplePath;
	int                mMaxPackets;

	// Parsed H.264 bitstream
	std::vector<uint8_t> mFileData;
	std::vector<size_t>  mNalOffsets;
	std::vector<size_t>  mNalSizes;

	// Decoder / timer objects  (owned by free stacks)
	qcap2_video_decoder_t*  mpVdec;
	qcap2_event_t*          mpVdecEvent;
	qcap2_timer_t*          mpTimer;

	// Pre-allocated packet buffers (owned by free stacks)
	std::vector<qcap2_rcbuffer_t*> mPacketPool;
	int64_t                        mPacketPoolNext;

	// Configuration
	ULONG    mEncoderType;
	ULONG    mEncoderFormat;
	ULONG    mWidth;
	ULONG    mHeight;
	int64_t  mBenchTimeoutMs;   // quiescent period (ms) before declaring done

	// Runtime counters (written by callbacks, read after completion)
	int64_t  mDecodeStartTime;  // _clk() of first decoded frame pop
	int64_t  mDecodeEndTime;    // _clk() when cooldown expires
	int64_t  mLastPopTime;      // _clk() of most recent pop
	int      mPushedCount;
	int      mPopCount;
	bool     mAllPushed;
	bool     mBenchDone;

	explicit VideoDecoderBenchmark()
		: mSamplePath(NULL)
		, mMaxPackets(64)
		, mpVdec(NULL)
		, mpVdecEvent(NULL)
		, mpTimer(NULL)
		, mPacketPoolNext(0)
		, mEncoderType(QCAP_ENCODER_TYPE_SOFTWARE)
		, mEncoderFormat(QCAP_ENCODER_FORMAT_H264)
		, mWidth(1920)
		, mHeight(1080)
		, mBenchTimeoutMs(3000)
		, mDecodeStartTime(0)
		, mDecodeEndTime(0)
		, mLastPopTime(0)
		, mPushedCount(0)
		, mPopCount(0)
		, mAllPushed(false)
		, mBenchDone(false)
	{
	}

	~VideoDecoderBenchmark() {
	}

	// -----------------------------------------------------------------------
	// Load & parse an H.264 raw file into NAL-unit boundaries.
	// -----------------------------------------------------------------------
	bool LoadAndParse(const char* pszPath) {
		LOGI("Loading sample: %s", pszPath);
		mSamplePath = pszPath;

		std::ifstream ifs(pszPath, std::ios::binary | std::ios::ate);
		if(!ifs) {
			LOGE("Failed to open file: %s", pszPath);
			return false;
		}
		size_t nSize = (size_t)ifs.tellg();
		ifs.seekg(0, std::ios::beg);

		mFileData.resize(nSize);
		ifs.read((char*)mFileData.data(), nSize);
		ifs.close();

		LOGI("File size: %zu bytes", nSize);

		// Find NAL start codes
		mNalOffsets.clear();
		mNalSizes.clear();
		for(size_t i = 0; i < nSize - 3; ) {
			if(mFileData[i] == 0 && mFileData[i+1] == 0) {
				int prefixLen = 0;
				if(mFileData[i+2] == 1) {
					prefixLen = 3;
				} else if(i < nSize - 4 && mFileData[i+2] == 0 && mFileData[i+3] == 1) {
					prefixLen = 4;
				}

				if(prefixLen > 0) {
					mNalOffsets.push_back(i);
					if(mNalOffsets.size() > 1)
						mNalSizes.back() = i - mNalOffsets[mNalOffsets.size() - 2];
					mNalSizes.push_back(0);
					i += prefixLen;
				} else {
					i++;
				}
			} else {
				i++;
			}
		}

		if(mNalOffsets.empty()) {
			LOGE("No H.264 start code found in file");
			return false;
		}
		mNalSizes.back() = nSize - mNalOffsets.back();
		LOGI("Found %zu NAL units", mNalOffsets.size());
		return true;
	}

	// -----------------------------------------------------------------------
	// Setup (must be invoked inside the event-handler context, e.g. OnStart)
	// Creates the decoder, timer, packet pool, and registers callbacks.
	// -----------------------------------------------------------------------
	QRESULT Setup(free_stack_t& _FreeStack_, qcap2_event_handlers_t* pEventHandlers) {
		QRESULT qres;

		mDecodeStartTime = 0;
		mDecodeEndTime   = 0;
		mLastPopTime     = 0;
		mPushedCount     = 0;
		mPopCount        = 0;
		mAllPushed       = false;
		mBenchDone       = false;
		mPacketPoolNext  = 0;

		switch(1) { case 1:
			// ---- decoder event ----
			qres = NewEvent(_FreeStack_, &mpVdecEvent);
			if(qres != QCAP_RS_SUCCESSFUL) {
				LOGE("NewEvent() failed, qres=%d", qres);
				break;
			}

			// ---- timer ----
			mpTimer = qcap2_timer_new();
			_FreeStack_ += [this]() { qcap2_timer_delete(mpTimer); };
			qres = qcap2_timer_start(mpTimer);
			if(qres != QCAP_RS_SUCCESSFUL) {
				LOGE("qcap2_timer_start() failed, qres=%d", qres);
				break;
			}
			_FreeStack_ += [this]() {
				QRESULT qr = qcap2_timer_stop(mpTimer);
				if(qr != QCAP_RS_SUCCESSFUL)
					LOGE("qcap2_timer_stop() failed, qres=%d", qr);
			};

			// ---- decoder ----
			mpVdec = qcap2_video_decoder_new();
			_FreeStack_ += [this]() { qcap2_video_decoder_delete(mpVdec); };

			{
				std::shared_ptr<qcap2_video_encoder_property_t> pProp(
					qcap2_video_encoder_property_new(),
					qcap2_video_encoder_property_delete);

				qcap2_video_encoder_property_set_type(pProp.get(), mEncoderType);
				qcap2_video_encoder_property_set_format(pProp.get(), mEncoderFormat);
				qcap2_video_encoder_property_set_resolution(pProp.get(), mWidth, mHeight);
				qcap2_video_decoder_set_video_property(mpVdec, pProp.get());
			}

			qcap2_video_decoder_set_event(mpVdec, mpVdecEvent);
			int nFrames = (int)mNalOffsets.size();
			qcap2_video_decoder_set_frame_count(mpVdec, nFrames > 64 ? 64 : nFrames);
			qcap2_video_decoder_set_packet_count(mpVdec, 64);

			qres = qcap2_video_decoder_start(mpVdec);
			if(qres != QCAP_RS_SUCCESSFUL) {
				LOGE("qcap2_video_decoder_start() failed, qres=%d", qres);
				break;
			}
			_FreeStack_ += [this]() {
				QRESULT qr = qcap2_video_decoder_stop(mpVdec);
				if(qr != QCAP_RS_SUCCESSFUL)
					LOGE("qcap2_video_decoder_stop() failed, qres=%d", qr);
			};

			// ---- pre-allocate packet buffers ----
			mPacketPool.clear();
			for(int i = 0; i < mMaxPackets; i++) {
				qcap2_rcbuffer_t* pBuf = qcap2_rcbuffer_new_av_packet();
				if(!pBuf) {
					LOGE("qcap2_rcbuffer_new_av_packet() failed at index %d", i);
					qres = QCAP_RS_ERROR_OUT_OF_MEMORY;
					break;
				}
				_FreeStack_ += [pBuf]() { qcap2_rcbuffer_release(pBuf); };
				mPacketPool.push_back(pBuf);
			}
			if(qres != QCAP_RS_SUCCESSFUL)
				break;

			// ---- register timer callback ----
			qres = __testkit__::AddTimerHandler(_FreeStack_, pEventHandlers, mpTimer,
				std::bind(&self_t::OnTimer, this));
			if(qres != QCAP_RS_SUCCESSFUL) {
				LOGE("AddTimerHandler() failed, qres=%d", qres);
				break;
			}

			// ---- register decoder event callback ----
			qres = AddEventHandler(_FreeStack_, pEventHandlers, mpVdecEvent,
				std::bind(&self_t::OnVdecEvent, this));
			if(qres != QCAP_RS_SUCCESSFUL) {
				LOGE("AddEventHandler(vdec) failed, qres=%d", qres);
				break;
			}

			// ---- kick timer ----
			qres = qcap2_timer_next(mpTimer, 1000);
			if(qres != QCAP_RS_SUCCESSFUL) {
				LOGE("qcap2_timer_next() failed, qres=%d", qres);
				break;
			}
		}

		return qres;
	}

	// -----------------------------------------------------------------------
	// WaitForCompletion -- block until the benchmark finishes.
	// Called from DoWork() AFTER ExecInEventHandlers(OnStart) returns.
	// -----------------------------------------------------------------------
	void WaitForCompletion() {
		LOGI("Benchmark running (encoder type 0x%08X)...", mEncoderType);
		wait_for_test_finish([this](int ch) -> bool {
			return !mBenchDone;
		}, 1000000LL, 100LL);
	}

	// -----------------------------------------------------------------------
	// Timer tick callback: push packets until queue full, check completion.
	// -----------------------------------------------------------------------
	QRETURN OnTimer() {
		QRESULT qres;

		switch(1) { case 1:
			uint64_t nExpirations;
			qres = qcap2_timer_wait(mpTimer, &nExpirations);
			if(qres != QCAP_RS_SUCCESSFUL)
				break;

			qres = qcap2_timer_next(mpTimer, 1000);
			if(qres != QCAP_RS_SUCCESSFUL)
				break;

			// Push phase
			if(!mAllPushed) {
				while(mPushedCount < (int)mNalOffsets.size()) {
					qcap2_rcbuffer_t* pBuf = mPacketPool[mPacketPoolNext % mMaxPackets];
					mPacketPoolNext++;

					std::shared_ptr<qcap2_av_packet_t> pPkt(
						(qcap2_av_packet_t*)qcap2_rcbuffer_lock_data(pBuf),
						[pBuf](qcap2_av_packet_t*) { qcap2_rcbuffer_unlock_data(pBuf); });

					int nSize = (int)mNalSizes[mPushedCount];
					if(!qcap2_av_packet_alloc_buffer(pPkt.get(), nSize)) {
						LOGE("av_packet_alloc_buffer(%d) failed", nSize);
						break;
					}

					uint8_t* pData;
					int nDataSize;
					qcap2_av_packet_get_buffer(pPkt.get(), &pData, &nDataSize);
					memcpy(pData, mFileData.data() + mNalOffsets[mPushedCount], nSize);

					BOOL bKeyFrame = (mPushedCount == 0) ? TRUE : FALSE;
					qcap2_av_packet_set_property(pPkt.get(), 0, bKeyFrame);
					qcap2_av_packet_set_pts(pPkt.get(), mPushedCount * 100000LL);
					qcap2_av_packet_set_dts(pPkt.get(), mPushedCount * 100000LL);

					qres = qcap2_video_decoder_push(mpVdec, pBuf);
					if(qres != QCAP_RS_SUCCESSFUL) {
						if(qres == QCAP_RS_ERROR_TIMEOUT) {
							mPacketPoolNext--;
							break;
						}
						LOGE("qcap2_video_decoder_push() failed, qres=%d", qres);
						break;
					}

					mPushedCount++;
					if(mPushedCount >= (int)mNalOffsets.size()) {
						mAllPushed = true;
						LOGI("All %d packets pushed.", mPushedCount);
					}
				}
			}

			// After all pushes, wait for quiescent period then declare done.
			if(mAllPushed && mPopCount > 0 && !mBenchDone) {
				int64_t now = _clk();
				if(now - mLastPopTime > mBenchTimeoutMs * 1000LL) {
					mDecodeEndTime = mLastPopTime;
					mBenchDone = true;
					LOGI("Benchmark completed (%d frames decoded).", mPopCount);
				}
			}
		}

		return QCAP_RT_OK;
	}

	// -----------------------------------------------------------------------
	// Decoder output: pop all available decoded frames.
	// -----------------------------------------------------------------------
	QRETURN OnVdecEvent() {
		QRESULT qres;

		switch(1) { case 1:
			while(true) {
				qcap2_rcbuffer_t* pRCBuffer;
				qres = qcap2_video_decoder_pop(mpVdec, &pRCBuffer);
				if(qres != QCAP_RS_SUCCESSFUL) {
					if(qres == QCAP_RS_ERROR_TIMEOUT)
						break;
					LOGE("qcap2_video_decoder_pop() failed, qres=%d", qres);
					break;
				}
				std::shared_ptr<qcap2_rcbuffer_t> pRCBuffer_(pRCBuffer,
					qcap2_rcbuffer_release);

				mPopCount++;
				mLastPopTime = _clk();

				if(mPopCount == 1)
					mDecodeStartTime = mLastPopTime;

				if((mPopCount % 30) == 0) {
					int64_t elapsed = mLastPopTime - mDecodeStartTime;
					if(elapsed > 0) {
						double fps = (double)mPopCount / (elapsed / 1000000.0);
						LOGI("Decoded %d frames (%.1f fps)", mPopCount, fps);
					}
				}
			}
		}

		return QCAP_RT_OK;
	}

	// -----------------------------------------------------------------------
	// PrintResults
	// -----------------------------------------------------------------------
	void PrintResults(const char* pLabel) {
		int64_t totalTime = mDecodeEndTime - mDecodeStartTime;
		double  totalSec  = totalTime / 1000000.0;
		double  fps       = (totalSec > 0.0) ? (mPopCount / totalSec) : 0.0;

		LOGI("");
		LOGI("=== %s ===", pLabel);
		LOGI("  Encoder type   : 0x%08X", mEncoderType);
		LOGI("  Source         : %s", mSamplePath);
		LOGI("  NAL units      : %zu", mNalOffsets.size());
		LOGI("  Frames decoded : %d", mPopCount);
		LOGI("  Total time     : %.3f s", totalSec);
		LOGI("  Average FPS    : %.1f", fps);
		LOGI("  Start time     : %lld us", mDecodeStartTime);
		LOGI("  End time       : %lld us", mDecodeEndTime);
		LOGI("");
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

	bool running;

	int Run() {
		int TEST_CASE;
		ZZ_ENV_INIT(TEST_CASE, 0);

		if(TEST_CASE != 0) {
			DoCase(TEST_CASE);
			return 0;
		}

		running = true;
		LOGI("Press 'q' to exit.");
		LOGI("  1 - Benchmark QCAP_ENCODER_TYPE_SOFTWARE");
		LOGI("  2 - Benchmark QCAP_ENCODER_TYPE_V4L2NVCODEC");
		while(running) {
			char ch;
			int fd_stdin = 0;
			fd_set readfds;
			FD_ZERO(&readfds);
			int fd_max = -1;
			if(fd_stdin > fd_max) fd_max = fd_stdin;
			FD_SET(fd_stdin, &readfds);

			int err = select(fd_max + 1, &readfds, NULL, NULL, NULL);
			if(err < 0) {
				LOGE("select() failed! err=%d", err);
				break;
			}

			if(FD_ISSET(fd_stdin, &readfds)) {
				ch = getchar();
				switch(ch) {
				case 'q': running = false; break;
				case '0': case '1': case '2': case '3': case '4':
				case '5': case '6': case '7': case '8': case '9':
					DoCase(ch - '0'); break;
				}
			}
		}
		return 0;
	}

	void DoCase(int t) {
		LOGD("DoCase(%d)", t);
		switch(t) {
		case 1: mTestCase1.DoWork(); break;
		case 2: mTestCase2.DoWork(); break;
		}
	}

	static const char* GetSamplePath() {
		const char* pEnv = getenv("VDEC_BENCH_SAMPLE");
		if(pEnv && pEnv[0]) return pEnv;
		return "/usr/src/jetson_multimedia_api/data/Video/sample_outdoor_car_1080p_10fps.h264";
	}

	// ---- Test case 1: QCAP_ENCODER_TYPE_SOFTWARE ----
	struct TestCase1 : public TestCase {
		typedef TestCase1 self_t;
		typedef TestCase super_t;

		VideoDecoderBenchmark mBench;

		void DoWork() {
			QRESULT qres;

			if(!mBench.LoadAndParse(GetSamplePath())) {
				LOGE("LoadAndParse() failed");
				return;
			}

			switch(1) { case 1:
				free_stack_t& _FreeStack_ = _FreeStack_main_;

				qres = StartEventHandlers();
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("StartEventHandlers() failed, qres=%d", qres);
					break;
				}
				ZzUtils::Scoped ZZ_GUARD_NAME([&]() {
					OnExitEventHandlers();
				});

				QRESULT qres_evt = QCAP_RS_SUCCESSFUL;
				qres = ExecInEventHandlers(std::bind(&self_t::OnStart, this,
					std::ref(_FreeStack_evt_), std::ref(qres_evt)));
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("ExecInEventHandlers() failed, qres=%d", qres);
					break;
				}
				if(qres_evt != QCAP_RS_SUCCESSFUL) {
					break;
				}

				mBench.WaitForCompletion();
				mBench.PrintResults("Software Decoder");
			}

			_FreeStack_main_.flush();
		}

		QRETURN OnStart(free_stack_t& _FreeStack_, QRESULT& qres) {
			mBench.mEncoderType   = QCAP_ENCODER_TYPE_SOFTWARE;
			mBench.mEncoderFormat = QCAP_ENCODER_FORMAT_H264;
			mBench.mWidth         = 1920;
			mBench.mHeight        = 1080;

			qres = mBench.Setup(_FreeStack_, pEventHandlers);
			return QCAP_RT_OK;
		}
	} mTestCase1;

	// ---- Test case 2: QCAP_ENCODER_TYPE_V4L2NVCODEC ----
	struct TestCase2 : public TestCase {
		typedef TestCase2 self_t;
		typedef TestCase super_t;

		VideoDecoderBenchmark mBench;

		void DoWork() {
			QRESULT qres;

			if(!mBench.LoadAndParse(GetSamplePath())) {
				LOGE("LoadAndParse() failed");
				return;
			}

			switch(1) { case 1:
				free_stack_t& _FreeStack_ = _FreeStack_main_;

				qres = StartEventHandlers();
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("StartEventHandlers() failed, qres=%d", qres);
					break;
				}
				ZzUtils::Scoped ZZ_GUARD_NAME([&]() {
					OnExitEventHandlers();
				});

				QRESULT qres_evt = QCAP_RS_SUCCESSFUL;
				qres = ExecInEventHandlers(std::bind(&self_t::OnStart, this,
					std::ref(_FreeStack_evt_), std::ref(qres_evt)));
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("ExecInEventHandlers() failed, qres=%d", qres);
					break;
				}
				if(qres_evt != QCAP_RS_SUCCESSFUL) {
					break;
				}

				mBench.WaitForCompletion();
				mBench.PrintResults("V4L2 NvCodec Decoder");
			}

			_FreeStack_main_.flush();
		}

		QRETURN OnStart(free_stack_t& _FreeStack_, QRESULT& qres) {
			mBench.mEncoderType   = QCAP_ENCODER_TYPE_V4L2NVCODEC;
			mBench.mEncoderFormat = QCAP_ENCODER_FORMAT_H264;
			mBench.mWidth         = 1920;
			mBench.mHeight        = 1080;

			qres = mBench.Setup(_FreeStack_, pEventHandlers);
			return QCAP_RT_OK;
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
