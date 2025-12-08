#include "qcap2.h"
#include "qcap.linux.h"
#include "qcap2.user.h"

#include "ZzLog.h"
#include "ZzModules.h"
#include "ZzClock.h"
#include "ZzStats.h"
#include "ZzUtils.h"
#include "testkit.h"

ZZ_INIT_LOG("test-graphics");

int g_argc = 0;
char** g_argv = NULL;

ZZ_MODULE_DECL(__zz_log__);

using namespace __zz_clock__;
using __testkit__::wait_for_test_finish;
using __testkit__::free_stack_t;
using __testkit__::callback_t;
using __testkit__::tick_ctrl_t;
using __testkit__::TestCase;
using __testkit__::NewEvent;

namespace __test_graphics__ {
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

using namespace __test_graphics__;

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

	struct TestCase1 : public TestCase {
		typedef TestCase1 self_t;
		typedef TestCase super_t;

		void DoWork() {
			QRESULT qres;

			LOGD("%s::%s", typeid(self_t).name(), __FUNCTION__);

			switch(1) { case 1:
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
					return true;
				}, 1000000LL, 10LL);
			}

			_FreeStack_main_.flush();
		}

		QRETURN OnStart(free_stack_t& _FreeStack_, QRESULT& qres) {
			switch(1) { case 1:
				const int nBufferWidth = 512;
				const int nBufferHeight = 64;
				// const ULONG nColorSpaceType = QCAP_COLORSPACE_TYPE_Y8;
				// const ULONG nColorSpaceType = QCAP_COLORSPACE_TYPE_ARGB32;
				const ULONG nColorSpaceType = QCAP_COLORSPACE_TYPE_ABGR32;
				// const ULONG nColorSpaceType = QCAP_COLORSPACE_TYPE_RGB444LE;
				// const ULONG nColorSpaceType = QCAP_COLORSPACE_TYPE_BGR444LE;

				qcap2_rcbuffer_t* pRCBuffer;
				qres = __testkit__::new_video_sysbuf(_FreeStack_, nColorSpaceType, nBufferWidth, nBufferHeight, &pRCBuffer);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): __testkit__::new_video_sysbuf() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
				// qcap2_fill_video_test_pattern(pRCBuffer, QCAP2_TEST_PATTERN_BLACK);

				qcap2_font_atlas_t* pFontAtlas = qcap2_font_atlas_new();
				_FreeStack_ += [pFontAtlas]() {
					qcap2_font_atlas_delete(pFontAtlas);
				};

				qcap2_font_atlas_set_family_name(pFontAtlas, "Ubuntu");
				// qcap2_font_atlas_set_font_file(pFontAtlas, "/opt/fonts/Ubuntu-R.ttf");
				qcap2_font_atlas_set_char_size(pFontAtlas, 16);
				qcap2_font_atlas_set_dpi(pFontAtlas, 96);
				qcap2_font_atlas_set_atlas_size(pFontAtlas, 1024, 1024);

				qres = qcap2_font_atlas_start(pFontAtlas);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_font_atlas_start() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
				_FreeStack_ += [pFontAtlas]() {
					QRESULT qres;

					qres = qcap2_font_atlas_stop(pFontAtlas);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): qcap2_font_atlas_stop() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					}
				};

				qcap2_graphics_t* pGraphics = qcap2_graphics_new();
				_FreeStack_ += [pGraphics]() {
					qcap2_graphics_delete(pGraphics);
				};

				qcap2_graphics_set_backend_type(pGraphics, QCAP2_GRAPHICS_BACKEND_TYPE_DEFAULT);
				qres = qcap2_graphics_start(pGraphics);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_graphics_start() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
				_FreeStack_ += [pGraphics]() {
					QRESULT qres;

					qres = qcap2_graphics_stop(pGraphics);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): qcap2_graphics_stop() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					}
				};

				qres = qcap2_graphics_begin(pGraphics, pRCBuffer);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_graphics_begin() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				qcap2_graphics_set_color(pGraphics, 0xFFAABBCC);
				qres = qcap2_graphics_fill_rect(pGraphics, 0, 0, -1, -1);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_graphics_fill_rect() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				qcap2_graphics_set_font_atlas(pGraphics, pFontAtlas);
				qcap2_graphics_set_color(pGraphics, 0xFF00FF00);
				qres = qcap2_graphics_draw_text(pGraphics, "This is green", -10, -10, -1, -1);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_graphics_draw_text() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				qcap2_graphics_set_color(pGraphics, 0xFFFF0000);
				qres = qcap2_graphics_draw_text(pGraphics, "This is red", 50, 10, -1, -1);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_graphics_draw_text() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				qcap2_graphics_set_color(pGraphics, 0xFF0000FF);
				qres = qcap2_graphics_draw_text(pGraphics, "This is blue", 100, 40, -1, -1);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_graphics_draw_text() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				qres = qcap2_graphics_end(pGraphics);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_graphics_end() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				qcap2_save_raw_video_frame(pRCBuffer, "testcase1");
			}

			return QCAP_RT_OK;
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
