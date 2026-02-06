#include "ZzLog.h"
#include "ZzModules.h"
#include "ZzClock.h"
#include "ZzStats.h"
#include "ZzUtils.h"
#include "testkit.h"

ZZ_INIT_LOG("sc6f0-dante-box");

int g_argc = 0;
char** g_argv = NULL;

ZZ_MODULE_DECL(__zz_log__);

using namespace __zz_clock__;

namespace __sc6f0_dante_box__ {
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

using namespace __sc6f0_dante_box__;
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

	struct avg_ticks {
		int64_t last_ts;
		int64_t ticks;
	};

	struct TestCase1 : public TestCase {
		typedef TestCase1 self_t;
		typedef TestCase super_t;

		ZzUtils::UserMM* pZzLabEnv;

		void DoWork() {
			QRESULT qres;

			LOGD("%s::%s", typeid(self_t).name(), __FUNCTION__);

			switch(1) { case 1:
				free_stack_t& _FreeStack_ = _FreeStack_main_;

				qres = StartEventHandlers();
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): StartEventHandlers() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
				ZzUtils::Scoped ZZ_GUARD_NAME([&]() {
					OnExitEventHandlers();
				} );

				QRESULT qres_evt = QCAP_RS_SUCCESSFUL;
				qres = ExecInEventHandlers(std::bind(&self_t::OnStart, this,
					std::ref(_FreeStack_evt_), std::ref(qres_evt)));
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): ExecInEventHandlers() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
				if(qres_evt != QCAP_RS_SUCCESSFUL) {
					break;
				}

				wait_for_test_finish([&](int ch) -> bool {
					QRESULT qres;

					switch(1) { case 1:
						break;
					}

					return true;
				}, 1000000LL, 10LL);
			}

			_FreeStack_main_.flush();
		}

		QRETURN OnStart(free_stack_t& _FreeStack_, QRESULT& qres) {
			switch(1) { case 1:
				const uintptr_t nBaseAddr = 0xB1010000;
				pZzLabEnv = new ZzUtils::UserMM(nBaseAddr, nBaseAddr + 0x1000 - 1, true);
				_FreeStack_ += [&]() {
					delete pZzLabEnv;
				};

				qcap2_timer_t* pTimer;
				qres = StartTimer(_FreeStack_, &pTimer);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): StartTimer() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
			}
			return QCAP_RT_OK;
		}

		QRESULT StartTimer(free_stack_t& _FreeStack_, qcap2_timer_t** ppTimer) {
			QRESULT qres;

			switch(1) { case 1:
				const int max_ticks = 4;

				avg_ticks** pAllStats = new avg_ticks*[max_ticks];
				_FreeStack_ += [pAllStats]() {
					delete [] pAllStats;
				};

				for(int i = 0;i < max_ticks;i++) {
					avg_ticks* pStats = new avg_ticks();
					_FreeStack_ += [pStats]() {
						delete pStats;
					};

					pStats->last_ts = 0;
					pStats->ticks = 0;

					pAllStats[i] = pStats;
				}

				qcap2_timer_t* pTimer = qcap2_timer_new();
				_FreeStack_ += [pTimer]() {
					qcap2_timer_delete(pTimer);
				};

				qcap2_timer_set_interval(pTimer, 1000000LL); // 1s

				qres = qcap2_timer_start(pTimer);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_timer_start() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
				_FreeStack_ += [pTimer]() {
					QRESULT qres;

					qres = qcap2_timer_stop(pTimer);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): qcap2_timer_stop() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					}
				};

				qres = AddTimerHandler(_FreeStack_, pTimer,
					std::bind(&self_t::OnTimer, this, pTimer, pAllStats, max_ticks));
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): AddTimerHandler() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				*ppTimer = pTimer;
			}

			return qres;
		}

#if 0
		int read_ticks(int index) {
			char path[PATH_MAX];
			sprintf(path, "/sys/devices/platform/amba_pl@0/b1010000.zzlab_env_ctrl/zdev_ticks%d", index);

			int nTicks;
			std::ifstream fsTicks(path);
			fsTicks >> nTicks;

			return nTicks;
		}
#endif

#if 1
		int read_ticks(int index) {
			int nTicks = pZzLabEnv->Read(0x0C + index * 4);
			return nTicks;
		}
#endif

		QRETURN OnTimer(qcap2_timer_t* pTimer, avg_ticks** pAllStats, int nMaxTicks) {
			QRESULT qres;
			int64_t now = _clk();

			switch(1) { case 1:
				uint64_t nExpirations;
				qres = qcap2_timer_wait(pTimer, &nExpirations);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_timer_wait() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				char buf[256];
				char* pTickMsg = buf;
				for(int i = 0;i < nMaxTicks;i++) {
					int ticks = read_ticks(i);

					avg_ticks* pStats = pAllStats[i];
					int64_t diff_ts = now - pStats->last_ts;
					int64_t total_ticks = ticks - pStats->ticks;
					pTickMsg += sprintf(pTickMsg, " [#%d %.2f] ", i, total_ticks * 1000.0 / diff_ts);
					pStats->ticks = ticks;
					pStats->last_ts = now;
				}

				LOGI("AVG-TICKS: %s", buf);
			}

			return QCAP_RT_OK;
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
