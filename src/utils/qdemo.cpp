#include "qcap2.h"
#include "qcap.linux.h"

#include "ZzLog.h"
#include "ZzModules.h"
#include "ZzClock.h"
#include "ZzStats.h"
#include "ZzUtils.h"
#include "testkit.h"

#include <stack>
#include <memory>
#include <functional>

ZZ_INIT_LOG("qdemo");

int g_argc = 0;
char** g_argv = NULL;

ZZ_MODULE_DECL(__zz_log__);

namespace __qdemo__ {
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

using namespace __qdemo__;
using __testkit__::free_stack_t;
using __testkit__::wait_for_test_finish;

struct App0 {
	typedef App0 self_t;

	static int Main() {
		self_t app;
		return app.Run();
	}

	App0() {
	}

	~App0() {
	}

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
				LOGE("%s(%d): select failed! err = %d", __FUNCTION__, __LINE__, err);
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

	struct TestCase1 {
		typedef TestCase1 self_t;

		free_stack_t oFreeStack;
		PVOID pDevice;

		TestCase1() {
		}

		~TestCase1() {
		}

		void DoWork() {
			QRESULT qres;

			switch(1) { case 1:
				qres = QCAP_CREATE("SC0710 PCI", 0, NULL, &pDevice);
				LOGD("QCAP_CREATE(), qres=%d", qres);
				oFreeStack += [&]() {
					QRESULT qres;

					qres = QCAP_DESTROY(pDevice);
					LOGD("QCAP_DESTROY(), qres=%d", qres);
					pDevice = NULL;
				};

				qres = QCAP_SET_VIDEO_INPUT(pDevice, QCAP_INPUT_TYPE_HDMI);
				LOGD("QCAP_SET_VIDEO_INPUT(), qres=%d", qres);

				qres = QCAP_SET_AUDIO_INPUT(pDevice, QCAP_INPUT_TYPE_EMBEDDED_AUDIO);
				LOGD("QCAP_SET_AUDIO_INPUT(), qres=%d", qres);

				qres = QCAP_RUN(pDevice);
				LOGD("QCAP_RUN: qres=%d", qres);
				oFreeStack += [&]() {
					QRESULT qres;

					qres = QCAP_STOP(pDevice);
					LOGD("QCAP_STOP(), qres=%d", qres);
				};

				wait_for_test_finish([&](int ch) -> bool {
					return true;
				}, 1000000LL, 10LL);

				oFreeStack.flush();
			}
		}
	} mTestCase1;
};

int main(int argc, char* argv[]) {
	g_argc = argc;
	g_argv = argv;

	int ret;
	{
		modules_init_t _modules_init_;
		ret = App0::Main();
	}

	return ret;
}
