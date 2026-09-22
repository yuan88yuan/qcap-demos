#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "qcap.linux.h"
#include "qcap2.h"

#include "ZzLog.h"
#include "ZzModules.h"
#include "ZzClock.h"
#include "ZzStats.h"
#include "ZzUtils.h"
#include "testkit.h"

ZZ_INIT_LOG("test-qcap-400hz");

int g_argc = 0;
char** g_argv = NULL;

ZZ_MODULE_DECL(__zz_log__);

using namespace __zz_clock__;

namespace __test_qcap_400hz__ {
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

using namespace __test_qcap_400hz__;
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

	// 400Hz sine tone via the legacy QCAP share-record API. The first
	// half generates audio frames with the qcap2 API (same construction
	// as test-alsa-400hz); the second half converts each qcap2_rcbuffer_t
	// with qcap2_rcbuffer_to_buffer() and feeds it to
	// QCAP_SET_AUDIO_SHARE_RECORD_UNCOMPRESSION_BUFFER(). The record is
	// started with DISPLAY so the resampled frames reach the share-record
	// audio renderer (QCAP_SET_AUDIO_SHARE_RECORD_SOUND_RENDERER).
	struct TestCase1 : public TestCase {
		typedef TestCase1 self_t;
		typedef TestCase super_t;

		void DoWork() {
			QRESULT qres;

			LOGD("%s::%s", typeid(self_t).name(), __FUNCTION__);

			switch(1) { case 1:
				free_stack_t& _FreeStack_ = _FreeStack_main_;

				int SOUND_NUM;
				int TONE_FREQ;
				int DURATION_SEC;
				ZZ_ENV_INIT(SOUND_NUM, -1);
				ZZ_ENV_INIT(TONE_FREQ, 400);
				ZZ_ENV_INIT(DURATION_SEC, 10);

				const ULONG nChannels = 2;
				const ULONG nSampleFmt = QCAP_SAMPLE_FMT_S16;
				const ULONG nSampleFrequency = 48000;
				const ULONG nFrameSize = 960; // 20ms @ 48kHz
				const int nBuffers = 8;

				int nSoundNum = SOUND_NUM;
				if(nSoundNum < 0) {
					qres = PickSoundRenderer(&nSoundNum);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): PickSoundRenderer() failed, qres=%d", __FUNCTION__, __LINE__, qres);
						break;
					}
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

				qres = QCAP_SET_AUDIO_SHARE_RECORD_PROPERTY_EX(0,
					QCAP_ENCODER_TYPE_SOFTWARE, QCAP_ENCODER_FORMAT_AAC,
					nChannels, 16, nSampleFrequency, 128 * 1000);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): QCAP_SET_AUDIO_SHARE_RECORD_PROPERTY_EX() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				qres = QCAP_SET_AUDIO_SHARE_RECORD_SOUND_RENDERER(0, nSoundNum);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): QCAP_SET_AUDIO_SHARE_RECORD_SOUND_RENDERER() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				qres = QCAP_START_SHARE_RECORD(0, NULL,
					QCAP_RECORD_FLAG_DISPLAY | QCAP_RECORD_FLAG_AUDIO_ONLY, 0.0, 0.0);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): QCAP_START_SHARE_RECORD() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
				_FreeStack_ += []() {
					QRESULT qres = QCAP_STOP_SHARE_RECORD(0);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("QCAP_STOP_SHARE_RECORD() failed, qres=%d", qres);
					}
				};

				int64_t nStart = _clk();
				int nIndex = 0;
				double dPhase = 0.0;

				LOGI("Playing %dHz tone on renderer %d for %d sec (press 'q' to stop)...",
					TONE_FREQ, nSoundNum, DURATION_SEC);
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

						BYTE* pFrameBuffer;
						ULONG nFrameBufferLen;
						qcap2_rcbuffer_to_buffer(pRCBuffer, &pFrameBuffer, &nFrameBufferLen);

						double dSampleTime = _clk() / 1000000.0;
						qres = QCAP_SET_AUDIO_SHARE_RECORD_UNCOMPRESSION_BUFFER(0,
							pFrameBuffer, nFrameBufferLen, dSampleTime);
						if(qres != QCAP_RS_SUCCESSFUL) {
							LOGE("%s(%d): QCAP_SET_AUDIO_SHARE_RECORD_UNCOMPRESSION_BUFFER() failed, qres=%d",
								__FUNCTION__, __LINE__, qres);
							return false;
						}
					}

					return true;
				}, 20 * 1000LL, 1LL);
			}

			_FreeStack_main_.flush();
		}

		// Auto-pick: first "hw:" Output whose name mentions the USB
		// ("Audio") card, else the first "hw:" Output.
		QRESULT PickSoundRenderer(int* pSoundNum) {
			QRESULT qres = QCAP_RS_SUCCESSFUL;

			switch(1) { case 1:
				int nFirstHW = -1;
				int nUSBHW = -1;

				CHAR* strName = NULL;
				int i = 0;
				while(true) {
					qres = QCAP_SOUNDRENDERER_ENUMERATION(&strName, TRUE);
					if(qres != QCAP_RS_SUCCESSFUL || ! strName)
						break;

					LOGI("renderer[%d]: %s", i, strName);
					if(strncmp(strName, "hw:", 3) == 0) {
						if(nFirstHW < 0)
							nFirstHW = i;
						if(strstr(strName, "CARD=Audio") != NULL && nUSBHW < 0)
							nUSBHW = i;
					}
					i++;
				}
				qres = QCAP_RS_SUCCESSFUL;

				int nSoundNum = (nUSBHW >= 0) ? nUSBHW : nFirstHW;
				if(nSoundNum < 0) {
					LOGE("%s(%d): no hw: output found", __FUNCTION__, __LINE__);
					qres = QCAP_RS_ERROR_INVALID_PARAMETER;
					break;
				}

				LOGI("PickSoundRenderer: %d", nSoundNum);
				*pSoundNum = nSoundNum;
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
