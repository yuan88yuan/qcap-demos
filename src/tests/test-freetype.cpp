#include "qcap2.h"
#include "qcap.linux.h"
#include "qcap2.user.h"

#include "ZzLog.h"
#include "ZzModules.h"
#include "ZzClock.h"
#include "ZzStats.h"
#include "ZzUtils.h"
#include "testkit.h"

#if BUILD_WITH_HARFBUZZ
#include <hb-ft.h>
#endif

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_STROKER_H

#if BUILD_WITH_FONTCONFIG
#include <fontconfig/fontconfig.h>
#endif

#include <string>

ZZ_INIT_LOG("test-freetype");

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

namespace __test_freetype__ {
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

	std::string FindFontFile(const char* strFamilyName, int nWeight, int nSlant) {
		std::string ret;

#if BUILD_WITH_FONTCONFIG
		switch(1) { case 1:
			FcInit();
			ZzUtils::Scoped ZZ_GUARD_NAME(FcFini);

			std::shared_ptr<FcConfig> config(FcInitLoadConfigAndFonts(), FcConfigDestroy);
			std::shared_ptr<FcPattern> pattern(FcPatternCreate(), FcPatternDestroy);

			FcPatternAddString(pattern.get(), FC_FAMILY, (const FcChar8 *)strFamilyName);
			FcPatternAddInteger(pattern.get(), FC_WEIGHT, nWeight);
			FcPatternAddInteger(pattern.get(), FC_SLANT, nSlant);

			FcConfigSubstitute(config.get(), pattern.get(), FcMatchPattern);
			FcDefaultSubstitute(pattern.get());

			FcResult fcres;
			std::shared_ptr<FcPattern> font(FcFontMatch(config.get(), pattern.get(), &fcres), FcPatternDestroy);
			if(! font) {
				LOGE("%s(%d): FcFontMatch() failed, fcres=%d", __FUNCTION__, __LINE__, fcres);
				break;
			}

			FcChar8* file = NULL;
			if(FcPatternGetString(font.get(), FC_FILE, 0, &file) != FcResultMatch) {
				LOGE("%s(%d): FcPatternGetString() failed, fcres=%d", __FUNCTION__, __LINE__, fcres);
				break;
			}
			ret = (const char*)file;
		}
#endif // BUILD_WITH_FONTCONFIG

		return ret;
	}
}

using namespace __test_freetype__;

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

		qcap2_rcbuffer_t* pRCbuffer;

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
			FT_Error fterr;

			switch(1) { case 1:
				std::string strFamilyName = "DejaVu Serif";
				const int nFontSize = 24;
				const int DPI = 96;
				// const int nWeight = FC_WEIGHT_BOLD;
				const int nWeight = FC_WEIGHT_NORMAL;
				// const int nSlant = FC_SLANT_ITALIC; 
				const int nSlant = FC_SLANT_ROMAN; 
				std::string strText = "Abcj,def";
				const int nBufferWidth = 1024;
				const int nBufferHeight = 512;

				qcap2_rcbuffer_t* pRCBuffer;
				qres = __testkit__::new_video_sysbuf(_FreeStack_, QCAP_COLORSPACE_TYPE_Y8, nBufferWidth, nBufferHeight, &pRCBuffer);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): __testkit__::new_video_sysbuf() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
				qcap2_fill_video_test_pattern(pRCBuffer, QCAP2_TEST_PATTERN_BLACK);

				std::shared_ptr<qcap2_av_frame_t> pAVFrame(
					(qcap2_av_frame_t*)qcap2_rcbuffer_lock_data(pRCBuffer),
					[pRCBuffer](qcap2_av_frame_t*) {
						qcap2_rcbuffer_unlock_data(pRCBuffer);
					});
				uint8_t* pBuffer;
				int nStride;
				qcap2_av_frame_get_buffer(pAVFrame.get(), &pBuffer, &nStride);

				std::string strFontFile = FindFontFile(strFamilyName.c_str(), nWeight, nSlant);
				if(strFontFile.empty()) {
					LOGE("%s(%d): unexpected, strFontFile.empty()", __FUNCTION__, __LINE__);
					break;
				}

				LOGD("strFontFile=%s", strFontFile.c_str());

				FT_Library pFtLib;
				fterr = FT_Init_FreeType(&pFtLib);
				if(fterr) {
					LOGE("%s(%d): FT_Init_FreeType failed, fterr=%d", __FUNCTION__, __LINE__, fterr);
					break;
				}
				ZzUtils::Scoped ZZ_GUARD_NAME([pFtLib]() { FT_Done_FreeType(pFtLib); } );

				FT_Face pFtFace;
				fterr = FT_New_Face(pFtLib, strFontFile.c_str(), 0, &pFtFace);
				if(fterr) {
					LOGE("%s(%d): FT_New_Face failed, fterr=%d", __FUNCTION__, __LINE__, fterr);
					break;
				}
				ZzUtils::Scoped ZZ_GUARD_NAME([pFtFace]() { FT_Done_Face(pFtFace); } );

				fterr = FT_Set_Char_Size(pFtFace, nFontSize << 6, nFontSize << 6, DPI, DPI);
				if(fterr) {
					LOGE("%s(%d): FT_Set_Char_Size failed, fterr=%d", __FUNCTION__, __LINE__, fterr);
					break;
				}

				LOGD("strText=\"%s\"", strText.c_str());

				std::shared_ptr<hb_font_t> pHbFont(hb_ft_font_create(pFtFace, NULL), hb_font_destroy);
				std::shared_ptr<hb_buffer_t> pHbBuffer(hb_buffer_create(), hb_buffer_destroy);
				hb_buffer_add_utf8(pHbBuffer.get(), strText.c_str(), -1, 0, -1);
				hb_buffer_set_direction(pHbBuffer.get(), HB_DIRECTION_LTR);

				hb_shape(pHbFont.get(), pHbBuffer.get(), NULL, 0);

				unsigned int glyph_count;
				hb_glyph_info_t *glyph_info = hb_buffer_get_glyph_infos(pHbBuffer.get(), &glyph_count);
				hb_glyph_position_t *glyph_pos = hb_buffer_get_glyph_positions(pHbBuffer.get(), &glyph_count);

				float pen_x = 0.0f;
				float pen_y = pFtFace->size->metrics.ascender / 64.0f + 488;
				LOGD("pen=(%.2f, %.2f)", pen_x, pen_y);

				for (unsigned int i = 0; i < glyph_count; i++) {
					unsigned int glyph_id = glyph_info[i].codepoint;

					if (FT_Load_Glyph(pFtFace, glyph_id, FT_LOAD_RENDER)) {
						continue;
					}

					FT_Pos hb_x_offset_26_6 = glyph_pos[i].x_offset;
					FT_Pos hb_y_offset_26_6 = glyph_pos[i].y_offset;

					float current_pen_x = pen_x + (float)hb_x_offset_26_6 / 64.0f;
					float current_pen_y = pen_y + (float)hb_y_offset_26_6 / 64.0f;

					int draw_x = (int)(current_pen_x + pFtFace->glyph->bitmap_left);
					int draw_y = (int)(current_pen_y - pFtFace->glyph->bitmap_top); 
					
					FT_Bitmap* pBitmap = &pFtFace->glyph->bitmap;
					switch(1) { case 1:
						int nWidth = pBitmap->width;
						int nRows = pBitmap->rows;
						int nLeft = draw_x;
						int nTop = draw_y;
						int nRight = draw_x + pBitmap->width;
						int nBottom = draw_y + pBitmap->rows;

						LOGD("%d,%d,%dx%d", nLeft, nTop, nWidth, nRows);

						// clipping
						if(nLeft < 0) { nWidth += nLeft; nLeft = 0; }
						if(nTop < 0) { nRows += nTop; nTop = 0; }
						if(nRight > nBufferWidth) { nWidth = nBufferWidth - nLeft; nRight = nBufferWidth; }
						if(nBottom > nBufferHeight) { nRows = nBufferHeight - nTop; nBottom = nBufferHeight; }

						LOGD("==>%d,%d,%dx%d", nLeft, nTop, nWidth, nRows);
						if(nWidth <= 0 || nRows <= 0)
							break;

						uint8_t* pSrc = (uint8_t*)pBitmap->buffer;
						uint8_t* pDst = pBuffer + draw_x + draw_y * nStride;
						for(int y = 0;y < nRows;y++, pSrc += pBitmap->pitch, pDst += nStride) {
							memcpy(pDst, pSrc, nWidth);
						}
					}

					LOGD("%d: glyph_id=%u, pen=(%.2f,%.2f), draw=(%d,%d), bitmap=(%dx%d, %d)",
						i, glyph_id, pen_x, pen_y, draw_x, draw_y,
						pBitmap->width, pBitmap->rows, pBitmap->pitch);

					pen_x += (float)glyph_pos[i].x_advance / 64.0f;
					pen_y += (float)glyph_pos[i].y_advance / 64.0f; // Y_advance 適用於垂直排版
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
