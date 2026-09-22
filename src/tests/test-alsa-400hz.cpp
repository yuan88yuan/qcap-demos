#include <stdlib.h>
#include <math.h>

#include "qcap2.h"
#include "qcap2.alsa.h"

#include "ZzLog.h"
#include "ZzModules.h"
#include "ZzClock.h"
#include "ZzStats.h"
#include "ZzUtils.h"
#include "testkit.h"

ZZ_INIT_LOG("test-alsa-400hz");

int g_argc = 0;
char** g_argv = NULL;

ZZ_MODULE_DECL(__zz_log__);

using namespace __zz_clock__;

namespace __test_alsa_400hz__ {
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

using namespace __test_alsa_400hz__;
using __testkit__::wait_for_test_finish;
using __testkit__::TestCase;
using __testkit__::free_stack_t;
using __testkit__::tick_ctrl_t;
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

	int Run() {
		int TEST_CASE;
		ZZ_ENV_INIT(TEST_CASE, 1);

		if(TEST_CASE != 0) {
			DoCase(TEST_CASE);

			return 0;
		}

		running = true;
		LOGI("Press 'q' to exit.");
		while(running) {
			char ch;

			int fd_stdin = 0; // stdin

			fd_set readfds;
			FD_ZERO(&readfds);

			int fd_max = -1;
			if(fd_stdin > fd_max) fd_max = fd_stdin;
			FD_SET(fd_stdin, &readfds);

			int err = select(fd_max + 1, &readfds, NULL, NULL, NULL);
			if (err < 0) {
				LOGE("%s(%d): select() failed! err = %d", __FUNCTION__, __LINE__, err);
				break;
			}

			if (FD_ISSET(fd_stdin, &readfds)) {
				ch = getchar();

				switch(ch) {
				case 'q':
					running = false;
					break;

				case '0':
				case '1':
				case '2':
				case '3':
				case '4':
				case '5':
				case '6':
				case '7':
				case '8':
				case '9':
					DoCase(ch - '0');
					break;
				}
			}
		}

		return 0;
	}

	void DoCase(int t) {
		LOGD("DoCase(%d)", t);

		switch(t) {
		case 1:
			mTestCase1.DoWork();
			break;
		}
	}

	// 400Hz sine tone generator driving the qcap2 ALSA audio sink.
	// Audio frames are constructed the same way as test-aenc's fake
	// source: pre-allocated av_frame rcbuffers filled with PCM samples.
	struct TestCase1 : public TestCase {
		typedef TestCase1 self_t;
		typedef TestCase super_t;

		void DoWork() {
			QRESULT qres;

			LOGD("%s::%s", typeid(self_t).name(), __FUNCTION__);

			switch(1) { case 1:
				free_stack_t& _FreeStack_ = _FreeStack_main_;

				int ALSA_CARD;
				int ALSA_DEVICE;
				int TONE_FREQ;
				int DURATION_SEC;
				ZZ_ENV_INIT(ALSA_CARD, 0);
				ZZ_ENV_INIT(ALSA_DEVICE, 0);
				ZZ_ENV_INIT(TONE_FREQ, 400);
				ZZ_ENV_INIT(DURATION_SEC, 10);

				const ULONG nChannels = 2;
				const ULONG nSampleFmt = QCAP_SAMPLE_FMT_S16;
				const ULONG nSampleFrequency = 48000;
				const ULONG nFrameSize = 960; // 20ms @ 48kHz
				const int nBuffers = 8;

				qcap2_audio_sink_t* pAsink;
				qres = StartAsink(_FreeStack_, nChannels, nSampleFrequency,
					ALSA_CARD, ALSA_DEVICE, &pAsink);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): StartAsink() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				qcap2_rcbuffer_t** pRCBuffers = new qcap2_rcbuffer_t*[nBuffers];
				_FreeStack_ += [pRCBuffers]() {
					delete[] pRCBuffers;
				};
				for(int i = 0;i < nBuffers;i++) {
					qcap2_rcbuffer_t* pRCBuffer;
					qres = __testkit__::new_audio_sysbuf(_FreeStack_,
						nChannels, nSampleFmt, nSampleFrequency, nFrameSize, &pRCBuffer);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): __testkit__::new_audio_sysbuf() failed, qres=%d", __FUNCTION__, __LINE__, qres);
						break;
					}

					pRCBuffers[i] = pRCBuffer;
				}
				if(qres != QCAP_RS_SUCCESSFUL)
					break;

				int64_t nStart = _clk();
				int nIndex = 0;
				double dPhase = 0.0;

				LOGI("Playing %dHz tone on hw:%d,%d for %d sec (press 'q' to stop)...",
					TONE_FREQ, ALSA_CARD, ALSA_DEVICE, DURATION_SEC);
				wait_for_test_finish([&](int ch) -> bool {
					QRESULT qres;

					switch(1) { case 1:
						// Feed one frame per timer tick only; stdin
						// characters (and EOF) never produce audio.
						if(ch != 0)
							break;

						if(DURATION_SEC > 0 && _clk() - nStart > (int64_t)DURATION_SEC * 1000000LL)
							return false;

						qcap2_rcbuffer_t* pRCBuffer = pRCBuffers[nIndex];
						nIndex = (nIndex + 1) % nBuffers;

						FillTone(pRCBuffer, nChannels, nFrameSize, TONE_FREQ, nSampleFrequency, dPhase);
						qcap2_av_frame_t* pAVFrame = (qcap2_av_frame_t*)qcap2_rcbuffer_get_data(pRCBuffer);
						qcap2_av_frame_set_pts(pAVFrame, _clk());

						qres = qcap2_audio_sink_push(pAsink, pRCBuffer);
						if(qres != QCAP_RS_SUCCESSFUL) {
							LOGE("%s(%d): qcap2_audio_sink_push() failed, qres=%d", __FUNCTION__, __LINE__, qres);
							return false;
						}
					}

					return true;
				}, 20 * 1000LL, 1LL);

				// Stop the sink before the buffers are freed: the ALSA
				// backend holds buffer references in its pending queue.
				// (The _FreeStack_ stop handler below is a no-op afterwards.)
				qres = qcap2_audio_sink_stop(pAsink);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_audio_sink_stop() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
			}

			_FreeStack_main_.flush();
		}

		QRESULT StartAsink(free_stack_t& _FreeStack_, ULONG nChannels, ULONG nSampleFrequency,
				int nCard, int nDevice, qcap2_audio_sink_t** ppAsink) {
			QRESULT qres = QCAP_RS_SUCCESSFUL;

			switch(1) { case 1:
				const ULONG nBitsPerSample = 16;
				const int nPeriodTime = 20 * 1000; // 20ms
				const int nBufferTime = 200 * 1000; // 200ms

				qcap2_audio_sink_t* pAsink = qcap2_audio_sink_new();
				_FreeStack_ += [pAsink]() {
					qcap2_audio_sink_delete(pAsink);
				};

				qcap2_audio_sink_set_backend_type(pAsink, QCAP2_AUDIO_SINK_BACKEND_TYPE_ALSA);
				qcap2_audio_sink_set_alsa_card(pAsink, nCard);
				qcap2_audio_sink_set_alsa_device(pAsink, nDevice);

				{
					std::shared_ptr<qcap2_audio_format_t> pAudioFormat(
						qcap2_audio_format_new(), qcap2_audio_format_delete);

					qcap2_audio_format_set_property(pAudioFormat.get(),
						nChannels, nBitsPerSample, nSampleFrequency);

					qcap2_audio_sink_set_audio_format(pAsink, pAudioFormat.get());
				}

				qcap2_audio_sink_set_period_time(pAsink, nPeriodTime);
				qcap2_audio_sink_set_buffer_time(pAsink, nBufferTime);

				qres = qcap2_audio_sink_start(pAsink);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_audio_sink_start() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
				_FreeStack_ += [pAsink]() {
					QRESULT qres;

					qres = qcap2_audio_sink_stop(pAsink);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): qcap2_audio_sink_stop() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					}
				};

				*ppAsink = pAsink;
			}

			return qres;
		}

		// Phase-continuous sine at -20dBFS, duplicated to all channels.
		static void FillTone(qcap2_rcbuffer_t* pRCBuffer, ULONG nChannels, ULONG nFrameSize,
				int nFreq, ULONG nSampleFrequency, double& dPhase) {
			qcap2_av_frame_t* pAVFrame = (qcap2_av_frame_t*)qcap2_rcbuffer_get_data(pRCBuffer);

			uint8_t* pBuffer;
			int nStride;
			qcap2_av_frame_get_buffer(pAVFrame, &pBuffer, &nStride);

			const double dStep = 2.0 * M_PI * (double)nFreq / (double)nSampleFrequency;
			int16_t* pPCM = (int16_t*)pBuffer;
			for(ULONG i = 0;i < nFrameSize;i++) {
				int16_t s = (int16_t)(3276.0 * sin(dPhase));
				dPhase += dStep;

				for(ULONG ch = 0;ch < nChannels;ch++)
					*pPCM++ = s;
			}

			dPhase = fmod(dPhase, 2.0 * M_PI);
		}
	} mTestCase1;
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
