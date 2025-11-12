#include "qcap.linux.h"
#include "qcap2.h"
#include "qcap2.user.h"
#include "qcap2.nvbuf.h"
#include "qcap2.cuda.h"

#include "ZzLog.h"
#include "ZzModules.h"
#include "ZzClock.h"
#include "ZzStats.h"
#include "ZzUtils.h"
#include "testkit.h"

#include <IpxGpuCodec/license.h>
#include <IpxGpuCodec/encoder.h>
#include <IpxGpuCodec/decoder.h>

ZZ_INIT_LOG("test-ipx");

int g_argc = 0;
char** g_argv = NULL;

ZZ_MODULE_DECL(__zz_log__);

using namespace __zz_clock__;
using __testkit__::wait_for_test_finish;
using __testkit__::TestCase;
using __testkit__::free_stack_t;
using __testkit__::tick_ctrl_t;
using __testkit__::NewEvent;
using __testkit__::new_video_nvbuf;
using __testkit__::spinlock_lock;
using __testkit__::spinlock_unlock;

namespace __test_ipx__ {
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

using namespace __test_ipx__;

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

#if 0
		case 2:
			mTestCase2.DoWork();
			break;

		case 3:
			mTestCase3.DoWork();
			break;
#endif
		}
	}

	struct TestCase1 : public TestCase {
		typedef TestCase1 self_t;
		typedef TestCase super_t;

		void DoWork() {
			LOGD("%s::%s", typeid(self_t).name(), __FUNCTION__);

			QRESULT qres;

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
				ipxgpucodec_error_t ipxerr;
				const char* pLicenseFn = "/opt/20240702-132607_672_Yuan-high-tech-devel_3686.lic";

				std::ifstream oLicenseFile(pLicenseFn);
				oLicenseFile.seekg(0, std::ios::end);
				size_t nLicenseFileLen = oLicenseFile.tellg();
				std::string strLicense(nLicenseFileLen, ' ');
				oLicenseFile.seekg(0);
				oLicenseFile.read(&strLicense[0], nLicenseFileLen);

				ipxerr = ipxgpucodec_load_license(strLicense.c_str(), nLicenseFileLen);
				if(ipxerr != IPXGPUCODEC_SUCCESS) {
					LOGE("%s(%d): ipxgpucodec_load_license() failed, ipxerr=%d",
						__FUNCTION__, __LINE__, ipxerr);
					break;
				}

				uint32_t nAttrSize;

				ipxgpucodec_bool_t bAllowEncode;
				ipxerr = ipxgpucodec_get_license_information(
					IPXGPUCODEC_LICENSE_ALLOWS_ENCODE,
					&bAllowEncode, sizeof(bAllowEncode), &nAttrSize);
				if(ipxerr != IPXGPUCODEC_SUCCESS) {
					LOGE("%s(%d): ipxgpucodec_get_license_information(IPXGPUCODEC_LICENSE_ALLOWS_ENCODE) failed, ipxerr=%d",
						__FUNCTION__, __LINE__, ipxerr);
					break;
				}
				LOGD("bAllowEncode=%d (%d)", (int)bAllowEncode, nAttrSize);

				ipxgpucodec_bool_t bAllowDecode;
				ipxerr = ipxgpucodec_get_license_information(
					IPXGPUCODEC_LICENSE_ALLOWS_DECODE,
					&bAllowDecode, sizeof(bAllowDecode), &nAttrSize);
				if(ipxerr != IPXGPUCODEC_SUCCESS) {
					LOGE("%s(%d): ipxgpucodec_get_license_information(IPXGPUCODEC_LICENSE_ALLOWS_DECODE) failed, ipxerr=%d",
						__FUNCTION__, __LINE__, ipxerr);
					break;
				}
				LOGD("bAllowDecode=%d (%d)", (int)bAllowDecode, nAttrSize);

				uint32_t nMaxAllowedImageWidth;
				ipxerr = ipxgpucodec_get_license_information(
					IPXGPUCODEC_LICENSE_MAX_ALLOWED_IMAGE_WIDTH,
					&nMaxAllowedImageWidth, sizeof(nMaxAllowedImageWidth), &nAttrSize);
				if(ipxerr != IPXGPUCODEC_SUCCESS) {
					LOGE("%s(%d): ipxgpucodec_get_license_information(IPXGPUCODEC_LICENSE_MAX_ALLOWED_IMAGE_WIDTH) failed, ipxerr=%d",
						__FUNCTION__, __LINE__, ipxerr);
					break;
				}
				LOGD("nMaxAllowedImageWidth=%d (%d)", (int)nMaxAllowedImageWidth, nAttrSize);

				uint32_t nMaxAllowedImageHeight;
				ipxerr = ipxgpucodec_get_license_information(
					IPXGPUCODEC_LICENSE_MAX_ALLOWED_IMAGE_HEIGHT,
					&nMaxAllowedImageHeight, sizeof(nMaxAllowedImageHeight), &nAttrSize);
				if(ipxerr != IPXGPUCODEC_SUCCESS) {
					LOGE("%s(%d): ipxgpucodec_get_license_information(IPXGPUCODEC_LICENSE_MAX_ALLOWED_IMAGE_HEIGHT) failed, ipxerr=%d",
						__FUNCTION__, __LINE__, ipxerr);
					break;
				}
				LOGD("nMaxAllowedImageHeight=%d, (%d)", (int)nMaxAllowedImageHeight, nAttrSize);

				ipxgpucodec_bool_t bAllowsCudaBackend;
				ipxerr = ipxgpucodec_get_license_information(
					IPXGPUCODEC_LICENSE_ALLOWS_CUDA_BACKEND,
					&bAllowsCudaBackend, sizeof(bAllowsCudaBackend), &nAttrSize);
				if(ipxerr != IPXGPUCODEC_SUCCESS) {
					LOGE("%s(%d): ipxgpucodec_get_license_information(IPXGPUCODEC_LICENSE_ALLOWS_CUDA_BACKEND) failed, ipxerr=%d",
						__FUNCTION__, __LINE__, ipxerr);
					break;
				}
				LOGD("bAllowsCudaBackend=%d (%d)", (int)bAllowsCudaBackend, nAttrSize);

				ipxgpucodec_bool_t bAllowsOpenCLBackend;
				ipxerr = ipxgpucodec_get_license_information(
					IPXGPUCODEC_LICENSE_ALLOWS_OPENCL_BACKEND,
					&bAllowsOpenCLBackend, sizeof(bAllowsOpenCLBackend), &nAttrSize);
				if(ipxerr != IPXGPUCODEC_SUCCESS) {
					LOGE("%s(%d): ipxgpucodec_get_license_information(IPXGPUCODEC_LICENSE_ALLOWS_OPENCL_BACKEND) failed, ipxerr=%d",
						__FUNCTION__, __LINE__, ipxerr);
					break;
				}
				LOGD("bAllowsOpenCLBackend=%d, (%d)", (int)bAllowsOpenCLBackend, nAttrSize);
			}

			return QCAP_RT_OK;
		}
	} mTestCase1;

	struct TestCase2 : public TestCase {
		typedef TestCase2 self_t;
		typedef TestCase super_t;

		ULONG nWidth;
		ULONG nHeight;
		NvBufSurfaceCreateParams oNVBufParam_vsrc;
		NvBufSurfaceCreateParams oNVBufParam_venc;
		int nBuffers_vsrc;
		int nBuffers_venc;
		float fBpp;

		std::vector<qcap2_rcbuffer_t*> oRCBuffers_vsrc;
		qcap2_rcbuffer_queue_t* pRCBufferQ_vsrc;

		std::vector<qcap2_rcbuffer_t*> oRCBuffers_venc;
		qcap2_rcbuffer_queue_t* pRCBufferQ_venc;

		std::atomic<bool> bPullerRunning;
		ipxgpucodec_encoder_t* pEncoder;
		size_t packed_image_size;
		uint32_t codestream_size;

		std::atomic<int> nVencCounter;
		ZzStatBitRate oBitRate;
		std::atomic_flag oLogLock;
		std::ofstream oLog;

		int nFrames;
		std::ofstream oCodeStream;
		std::ofstream oCodeStreamIdx;

		void DoWork() {
			LOGD("%s::%s", typeid(self_t).name(), __FUNCTION__);

			QRESULT qres;

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
			int err;
			ipxgpucodec_error_t ipxerr;
			cudaError_t cuerr;

			switch(1) { case 1:
				nWidth = 3840;
				nHeight = 2160;

				memset(&oNVBufParam_vsrc, 0, sizeof(oNVBufParam_vsrc));
				oNVBufParam_vsrc.width = nWidth;
				oNVBufParam_vsrc.height = nHeight;
				oNVBufParam_vsrc.layout = NVBUF_LAYOUT_PITCH;
				oNVBufParam_vsrc.memType = NVBUF_MEM_DEFAULT;
				oNVBufParam_vsrc.gpuId = 0;
				oNVBufParam_vsrc.colorFormat = NVBUF_COLOR_FORMAT_UYVY;

				nBuffers_vsrc = 2;
				fBpp = 1.8f;
				nBuffers_venc = 2;

				memset(&oNVBufParam_venc, 0, sizeof(oNVBufParam_venc));
				oNVBufParam_venc.width = (nWidth * fBpp) / 8;
				oNVBufParam_venc.height = nHeight;
				oNVBufParam_venc.layout = NVBUF_LAYOUT_PITCH;
				oNVBufParam_venc.memType = NVBUF_MEM_DEFAULT;
				oNVBufParam_venc.gpuId = 0;
				oNVBufParam_venc.colorFormat = NVBUF_COLOR_FORMAT_GRAY8;

				oBitRate.log_prefix = "venc";
				oBitRate.Reset();

				oLogLock.clear();
				nVencCounter.store(0);
				oLog.open("venc.log");
				_FreeStack_ += [&]() {
					oLog.close();
				};

				nFrames = 0;
				oCodeStream.open("codestream.jsx", std::ios::binary);
				_FreeStack_ += [&]() {
					oCodeStream.close();
				};

				oCodeStreamIdx.open("codestream.idx");
				_FreeStack_ += [&]() {
					oCodeStreamIdx.close();
				};

				std::ifstream oRawImage("3840x2160.uyvy", std::ios::binary);
				oRawImage.seekg(0, std::ios::end);
				size_t nRawImageSize = oRawImage.tellg();
				std::string strRawImage(nRawImageSize, ' ');
				oRawImage.seekg(0);
				oRawImage.read(&strRawImage[0], nRawImageSize);
				LOGD("nRawImageSize=%d", nRawImageSize);

				for(int i = 0;i < nBuffers_vsrc;i++) {
					qcap2_rcbuffer_t* pRCBuffer;
					qres = new_video_nvbuf(_FreeStack_, oNVBufParam_vsrc, &pRCBuffer);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): new_video_nvbuf() failed, qres=%d", __FUNCTION__, __LINE__, qres);
						break;
					}

					{
						qres = qcap2_rcbuffer_map_nvbuf(pRCBuffer, NVBUF_MAP_READ_WRITE);
						if(qres != QCAP_RS_SUCCESSFUL) {
							LOGE("%s(%d): qcap2_rcbuffer_map_nvbuf() failed, qres=%d", __FUNCTION__, __LINE__, qres);
							break;
						}
						ZzUtils::Scoped ZZ_GUARD_NAME([pRCBuffer]() {
							QRESULT qres;

							qres = qcap2_rcbuffer_unmap_nvbuf(pRCBuffer);
							if(qres != QCAP_RS_SUCCESSFUL) {
								LOGE("%s(%d): qcap2_rcbuffer_unmap_nvbuf() failed, qres=%d", __FUNCTION__, __LINE__, qres);
							}
						});

						std::shared_ptr<qcap2_av_frame_t> pAVFrame(
							(qcap2_av_frame_t*)qcap2_rcbuffer_lock_data(pRCBuffer),
							[pRCBuffer](qcap2_av_frame_t*) {
								qcap2_rcbuffer_unlock_data(pRCBuffer);
							});

						uint8_t* pData[4];
						int pLineSize[4];
						qcap2_av_frame_get_buffer1(pAVFrame.get(), pData, pLineSize);

						LOGD("%d: [%p %p %p %p] [%d,%d,%d,%d]", i,
							pData[0], pData[1], pData[2], pData[3],
							(int)pLineSize[0], (int)pLineSize[1], (int)pLineSize[2], (int)pLineSize[3]);

#if 1
						qres = qcap2_fill_video_test_pattern(pRCBuffer, QCAP2_TEST_PATTERN_0);
						if(qres != QCAP_RS_SUCCESSFUL) {
							LOGE("%s(%d): qcap2_fill_video_test_pattern() failed, qres=%d", __FUNCTION__, __LINE__, qres);
							break;
						}
#else
						memcpy(pData[0], &strRawImage[0], nRawImageSize);
#endif

						qcap2_rcbuffer_sync_nvbuf_for_device(pRCBuffer);

#if 0
						char fn[256];
						sprintf(fn, "src%06d", i);
						qcap2_save_raw_video_frame(pRCBuffer, fn);
#endif
					}

					oRCBuffers_vsrc.push_back(pRCBuffer);
				}
				_FreeStack_ += [&]() {
					oRCBuffers_vsrc.clear();
				};

				pRCBufferQ_vsrc = qcap2_rcbuffer_queue_new();
				_FreeStack_ += [&]() {
					qcap2_rcbuffer_queue_delete(pRCBufferQ_vsrc);
				};

				qcap2_rcbuffer_queue_set_max_buffers(pRCBufferQ_vsrc, nBuffers_vsrc);
				qcap2_rcbuffer_queue_set_buffers(pRCBufferQ_vsrc, &oRCBuffers_vsrc[0]);

				qres = qcap2_rcbuffer_queue_start(pRCBufferQ_vsrc);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_rcbuffer_queue_start() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
				_FreeStack_ += [&]() {
					QRESULT qres;

					LOGD("%s(%d):++++", __FUNCTION__, __LINE__);
					qres = qcap2_rcbuffer_queue_stop(pRCBufferQ_vsrc);
					LOGD("%s(%d):----", __FUNCTION__, __LINE__);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): qcap2_rcbuffer_queue_stop() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					}
				};

				for(int i = 0;i < nBuffers_venc;i++) {
					qcap2_rcbuffer_t* pRCBuffer;
					qres = new_video_nvbuf(_FreeStack_, oNVBufParam_venc, &pRCBuffer);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): new_video_nvbuf() failed, qres=%d", __FUNCTION__, __LINE__, qres);
						break;
					}

					oRCBuffers_venc.push_back(pRCBuffer);
				}
				_FreeStack_ += [&]() {
					oRCBuffers_venc.clear();
				};

				pRCBufferQ_venc = qcap2_rcbuffer_queue_new();
				_FreeStack_ += [&]() {
					qcap2_rcbuffer_queue_delete(pRCBufferQ_venc);
				};

				qcap2_rcbuffer_queue_set_max_buffers(pRCBufferQ_venc, nBuffers_venc);
				qcap2_rcbuffer_queue_set_buffers(pRCBufferQ_venc, &oRCBuffers_venc[0]);

				qres = qcap2_rcbuffer_queue_start(pRCBufferQ_venc);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_rcbuffer_queue_start() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
				_FreeStack_ += [&]() {
					QRESULT qres;

					LOGD("%s(%d):++++", __FUNCTION__, __LINE__);
					qres = qcap2_rcbuffer_queue_stop(pRCBufferQ_venc);
					LOGD("%s(%d):----", __FUNCTION__, __LINE__);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): qcap2_rcbuffer_queue_stop() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					}
				};

				ipxgpucodec_image_format_configuration_t* fmt_config = NULL;
				ipxerr = ipxgpucodec_create_image_format_configuration(
					IPXGPUCODEC_FMT_UYVY, &fmt_config);
				if(ipxerr != IPXGPUCODEC_SUCCESS) {
					LOGE("%s(%d): ipxgpucodec_create_image_format_configuration() failed, ipxerr=%d",
						__FUNCTION__, __LINE__, ipxerr);
					break;
				}
				_FreeStack_ += [&, fmt_config]() {
					ipxgpucodec_error_t ipxerr;

					ipxerr = ipxgpucodec_delete_image_format_configuration(fmt_config);
					if(ipxerr != IPXGPUCODEC_SUCCESS) {
						LOGE("%s(%d): ipxgpucodec_delete_image_format_configuration() failed, ipxerr=%d",
							__FUNCTION__, __LINE__, ipxerr);
					}
				};

				ipxerr = ipxgpucodec_get_image_format_size(
					fmt_config, nWidth, nHeight, &packed_image_size);
				if(ipxerr != IPXGPUCODEC_SUCCESS) {
					LOGE("%s(%d): ipxgpucodec_get_image_format_size() failed, ipxerr=%d",
						__FUNCTION__, __LINE__, ipxerr);
					break;
				}
				LOGD("packed_image_size=%d", packed_image_size);

				ipxgpucodec_picture_configuration_t* pic_config = NULL;
				ipxerr = ipxgpucodec_create_picture_configuration(
					IPXGPUCODEC_PIC_PROFILE_DEFAULT,
					nWidth, nHeight,
					IPXGPUCODEC_PIC_CHROMA_FORMAT_422,
					8, (nWidth * nHeight * fBpp) / 8, &pic_config);
				if(ipxerr != IPXGPUCODEC_SUCCESS) {
					const char* error_name = ipxgpucodec_error_name(ipxerr);
					const char* error_msg = ipxgpucodec_last_error_message();
					LOGE("%s(%d): ipxgpucodec_create_picture_configuration() failed, ipxerr=%d, %s, %s",
						__FUNCTION__, __LINE__, ipxerr, error_name, error_msg);
					break;
				}
				_FreeStack_ += [&, pic_config]() {
					ipxgpucodec_error_t ipxerr;

					ipxerr = ipxgpucodec_delete_picture_configuration(pic_config);
					if(ipxerr != IPXGPUCODEC_SUCCESS) {
						LOGE("%s(%d): ipxgpucodec_delete_picture_configuration() failed, ipxerr=%d",
							__FUNCTION__, __LINE__, ipxerr);
					}
				};

				uint32_t nAttrSize;

				ipxgpucodec_picture_profile_t pic_profile;
				ipxerr = ipxgpucodec_get_picture_configuration(pic_config,
					IPXGPUCODEC_PIC_PROFILE,
					&pic_profile,
					sizeof(pic_profile),
					&nAttrSize);
				if(ipxerr != IPXGPUCODEC_SUCCESS) {
					LOGE("%s(%d): ipxgpucodec_get_picture_configuration(IPXGPUCODEC_PIC_PROFILE) failed, ipxerr=%d",
						__FUNCTION__, __LINE__, ipxerr);
					break;
				}
				LOGD("pic_profile=%d (%d)", (int)pic_profile, nAttrSize);

				ipxerr = ipxgpucodec_get_picture_configuration(pic_config,
					IPXGPUCODEC_PIC_CODESTREAM_SIZE,
					&codestream_size,
					sizeof(codestream_size),
					&nAttrSize);
				if(ipxerr != IPXGPUCODEC_SUCCESS) {
					LOGE("%s(%d): ipxgpucodec_get_picture_configuration(IPXGPUCODEC_PIC_CODESTREAM_SIZE) failed, ipxerr=%d",
						__FUNCTION__, __LINE__, ipxerr);
					break;
				}
				LOGD("codestream_size=%d (%d)", codestream_size, nAttrSize);

				ipxgpucodec_picture_rate_allocation_optimization_t optimization = IPXGPUCODEC_PIC_RA_OPT_PSNR;
				ipxerr = ipxgpucodec_set_picture_configuration(pic_config,
					IPXGPUCODEC_PIC_RATE_ALLOCATION_OPTIMIZATION, &optimization, sizeof(optimization));
				if(ipxerr != IPXGPUCODEC_SUCCESS) {
					LOGE("%s(%d): ipxgpucodec_set_picture_configuration(IPXGPUCODEC_PIC_RATE_ALLOCATION_OPTIMIZATION) failed, ipxerr=%d",
						__FUNCTION__, __LINE__, ipxerr);
					break;
				}

				ipxgpucodec_encoder_configuration_t* enc_config = NULL;
				ipxerr = ipxgpucodec_enc_create_encoder_configuration(&enc_config);
				if(ipxerr != IPXGPUCODEC_SUCCESS) {
					LOGE("%s(%d): ipxgpucodec_enc_create_encoder_configuration() failed, ipxerr=%d",
						__FUNCTION__, __LINE__, ipxerr);
					break;
				}
				_FreeStack_ += [&, enc_config]() {
					ipxgpucodec_error_t ipxerr;

					ipxerr = ipxgpucodec_enc_delete_encoder_configuration(enc_config);
					if(ipxerr != IPXGPUCODEC_SUCCESS) {
						LOGE("%s(%d): ipxgpucodec_enc_delete_encoder_configuration() failed, ipxerr=%d",
							__FUNCTION__, __LINE__, ipxerr);
					}
				};

				// need to set encoder to use primary CUDA context (the one used by the CUDA runtime API), instead of creating its own CUDA context
				ipxgpucodec_bool_t use_primary_cuda_context = IPXGPUCODEC_TRUE;
				ipxerr = ipxgpucodec_enc_set_encoder_configuration(enc_config, IPXGPUCODEC_ENC_INTEROPERATION_PRIMARY_CUDA_CONTEXT, &use_primary_cuda_context, sizeof(use_primary_cuda_context));
				if(ipxerr != IPXGPUCODEC_SUCCESS) {
					LOGE("%s(%d): ipxgpucodec_enc_set_encoder_configuration(IPXGPUCODEC_ENC_INTEROPERATION_PRIMARY_CUDA_CONTEXT) failed, ipxerr=%d",
						__FUNCTION__, __LINE__, ipxerr);
					break;
				}

				// use CUDA buffer input and output mode: encoder will directly access the managed memory buffers, instead of explicitly doing a copy
				ipxgpucodec_enc_input_mode_t input_mode = IPXGPUCODEC_ENC_CUDA_BUFFER_INPUT;
				ipxerr = ipxgpucodec_enc_set_encoder_configuration(enc_config, IPXGPUCODEC_ENC_INPUT_MODE, &input_mode, sizeof(input_mode));
				if(ipxerr != IPXGPUCODEC_SUCCESS) {
					LOGE("%s(%d): ipxgpucodec_enc_set_encoder_configuration(IPXGPUCODEC_ENC_INPUT_MODE) failed, ipxerr=%d",
						__FUNCTION__, __LINE__, ipxerr);
					break;
				}

				ipxgpucodec_enc_output_mode_t output_mode = IPXGPUCODEC_ENC_CUDA_BUFFER_OUTPUT;
				ipxerr = ipxgpucodec_enc_set_encoder_configuration(enc_config, IPXGPUCODEC_ENC_OUTPUT_MODE, &output_mode, sizeof(output_mode));
				if(ipxerr != IPXGPUCODEC_SUCCESS) {
					LOGE("%s(%d): ipxgpucodec_enc_set_encoder_configuration(IPXGPUCODEC_ENC_OUTPUT_MODE) failed, ipxerr=%d",
						__FUNCTION__, __LINE__, ipxerr);
					break;
				}

				LOGD("Create encoder...");
				pEncoder = NULL;
				ipxerr = ipxgpucodec_enc_create_encoder(
					enc_config, fmt_config, pic_config, &pEncoder);
				if(ipxerr != IPXGPUCODEC_SUCCESS) {
					const char* error_name = ipxgpucodec_error_name(ipxerr);
					const char* error_msg = ipxgpucodec_last_error_message();
					LOGE("%s(%d): ipxgpucodec_enc_create_encoder() failed, ipxerr=%d, %s, %s",
						__FUNCTION__, __LINE__, ipxerr, error_name, error_msg);
					break;
				}
				_FreeStack_ += [&]() {
					ipxgpucodec_error_t ipxerr;

					LOGD("%s(%d):++++", __FUNCTION__, __LINE__);
					ipxerr = ipxgpucodec_enc_delete_encoder(pEncoder);
					LOGD("%s(%d):----", __FUNCTION__, __LINE__);
					if(ipxerr != IPXGPUCODEC_SUCCESS) {
						LOGE("%s(%d): ipxgpucodec_enc_delete_encoder() failed, ipxerr=%d",
							__FUNCTION__, __LINE__, ipxerr);
					}
				};

#if 0
				LOGD("Venc: first frame ++++");
				DoVsrc();
				DoVenc();
				LOGD("Venc: first frame ----");
#endif

				bPullerRunning.store(true);
				std::thread t1(std::bind(&self_t::Main_Puller, this));

#if 0
				wait_for_test_finish([&]() {
					DoVsrc();
				}, 1000000LL, 60LL);
#endif

				ipxerr = ipxgpucodec_enc_set_end_of_codestream(pEncoder, true);
				if(ipxerr != IPXGPUCODEC_SUCCESS) {
					LOGE("%s(%d): ipxgpucodec_enc_push_frame() failed, ipxerr=%d",
						__FUNCTION__, __LINE__, ipxerr);
					break;
				}

				LOGD("Wait for pusher ++++");
				bPullerRunning.store(false);
				t1.join();
				LOGD("Wait for pusher ----");
			}

			return QCAP_RT_OK;
		}

		void Main_Puller() {
			cudaFree(NULL);

			LOGD("%s(%d): ++++", __FUNCTION__, __LINE__);

			while(bPullerRunning.load()) {
				DoVenc();
			}

			LOGD("%s(%d): ----", __FUNCTION__, __LINE__);
		}

		int DoVsrc() {
			QRESULT qres;
			ipxgpucodec_error_t ipxerr;
			int nTasks = 0;
			int64_t now;

			switch(1) { case 1:
				qcap2_rcbuffer_t* pRCBuffer;
				qres = qcap2_rcbuffer_queue_pop(pRCBufferQ_vsrc, &pRCBuffer);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_rcbuffer_queue_pop() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				now = _clk();

				nTasks++;
				std::shared_ptr<qcap2_rcbuffer_t> pRCBuffer_(pRCBuffer, qcap2_rcbuffer_release);
				std::shared_ptr<qcap2_av_frame_t> pAVFrame(
					(qcap2_av_frame_t*)qcap2_rcbuffer_lock_data(pRCBuffer),
					[pRCBuffer](qcap2_av_frame_t*) {
						qcap2_rcbuffer_unlock_data(pRCBuffer);
					});

				uintptr_t pBufferSrc[3];
				int pPitchSrc[3];
				qres = qcap2_rcbuffer_get_mapped_eglbuffer(pRCBuffer, pBufferSrc, pPitchSrc);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("qcap2_rcbuffer_get_mapped_eglbuffer(), qres=%d", qres);
					break;
				}

#if 0
				LOGD("(%p, %p, %p), (%d, %d, %d)",
					pBufferSrc[0], pBufferSrc[1], pBufferSrc[2],
					pPitchSrc[0], pPitchSrc[1], pPitchSrc[2]);
#endif
				{
					int val = nVencCounter.fetch_add(1);

					char buf[256];
					ssize_t count = sprintf(buf, "venc[%.2f]: ++++ %d\n", now / 1000.0, val);

					spinlock_lock(oLogLock);
					oLog.write(buf, count);
					spinlock_unlock(oLogLock);
				}

				ipxerr = ipxgpucodec_enc_push_frame(pEncoder,
					(const void*)pBufferSrc[0], packed_image_size);
				if(ipxerr != IPXGPUCODEC_SUCCESS) {
					LOGE("%s(%d): ipxgpucodec_enc_push_frame() failed, ipxerr=%d",
						__FUNCTION__, __LINE__, ipxerr);
					break;
				}
			}

			return nTasks;
		}

		int DoVenc() {
			QRESULT qres;
			ipxgpucodec_error_t ipxerr;
			int nTasks = 0;
			int64_t now;
			uint8_t* pData;

			switch(1) { case 1:
				qcap2_rcbuffer_t* pRCBuffer;
				qres = qcap2_rcbuffer_queue_pop(pRCBufferQ_venc, &pRCBuffer);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_rcbuffer_queue_pop() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				now = _clk();

				nTasks++;
				std::shared_ptr<qcap2_rcbuffer_t> pRCBuffer_(pRCBuffer, qcap2_rcbuffer_release);
				std::shared_ptr<qcap2_av_frame_t> pAVFrame(
					(qcap2_av_frame_t*)qcap2_rcbuffer_lock_data(pRCBuffer),
					[pRCBuffer](qcap2_av_frame_t*) {
						qcap2_rcbuffer_unlock_data(pRCBuffer);
					});

				uintptr_t pBufferSrc[3];
				int pPitchSrc[3];
				qres = qcap2_rcbuffer_get_mapped_eglbuffer(pRCBuffer, pBufferSrc, pPitchSrc);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("qcap2_rcbuffer_get_mapped_eglbuffer(), qres=%d", qres);
					break;
				}

				ipxerr = ipxgpucodec_enc_pull_frame(pEncoder,
					(void*)pBufferSrc[0], codestream_size);
				if(ipxerr != IPXGPUCODEC_SUCCESS) {
					if(ipxerr == IPXGPUCODEC_ERR_END_OF_CODESTREAM)
						break;

					LOGE("%s(%d): ipxgpucodec_enc_pull_frame() failed, ipxerr=%d",
						__FUNCTION__, __LINE__, ipxerr);
					break;
				}

#if 0
				qcap2_rcbuffer_sync_nvbuf_for_cpu(pRCBuffer);
#endif

#if 1
				if(nFrames == 0) {
					uint8_t* pBuffer[4];
					int pStride[4];
					qcap2_av_frame_get_buffer1(pAVFrame.get(), pBuffer, pStride);

					uint8_t* pData = (uint8_t*)pBuffer[0];
					oCodeStream.write((const char*)pData, codestream_size);
					oCodeStreamIdx << codestream_size << std::endl;
				}
#endif

				nFrames++;

				now = _clk();
				oBitRate.Log(codestream_size * 8, now);

				{
					int val = nVencCounter.fetch_sub(1);

					char buf[256];
					ssize_t count = sprintf(buf, "venc[%.2f]: ---- %d\n", now / 1000.0, val);

					spinlock_lock(oLogLock);
					oLog.write(buf, count);
					spinlock_unlock(oLogLock);
				}

				nTasks++;
			}

			return nTasks;
		}
	} mTestCase2;

#if 0
	struct TestCase3 : public TestCase {
		typedef TestCase3 self_t;
		typedef TestCase super_t;

		int nFrames;
		std::ifstream oCodeStream;
		std::ifstream oCodeStreamIdx;

		uint32_t nWidth;
		uint32_t nHeight;
		ipxgpucodec_picture_chroma_format_t nChromaFormat;
		uint32_t nBitDepth;
		uint32_t codestream_size;
		uint32_t packed_image_size;

		NvBufSurfaceCreateParams oNVBufParam_vsrc;
		NvBufSurfaceCreateParams oNVBufParam_venc;
		int nBuffers_vsrc;
		int nBuffers_venc;

		std::vector<qcap2_rcbuffer_t*> oRCBuffers_vsrc;
		qcap2_rcbuffer_queue_t* pRCBufferQ_vsrc;

		std::vector<qcap2_rcbuffer_t*> oRCBuffers_venc;
		qcap2_rcbuffer_queue_t* pRCBufferQ_venc;

		std::atomic<bool> bPullerRunning;
		ipxgpucodec_decoder_t* pDecoder;

		std::atomic<int> nVdecCounter;
		ZzStatBitRate oBitRate;
		std::atomic_flag oLogLock;
		std::ofstream oLog;

		void DoWork() {
			LOGD("%s::%s", typeid(self_t).name(), __FUNCTION__);

			QRESULT qres;
			ipxgpucodec_error_t ipxerr;
			const char* pCodeStreamFn = "codestream.jsx";

			memset(&oNVBufParam_vsrc, 0, sizeof(oNVBufParam_vsrc));
			oNVBufParam_vsrc.layout = NVBUF_LAYOUT_PITCH;
			oNVBufParam_vsrc.memType = NVBUF_MEM_DEFAULT;
			oNVBufParam_vsrc.gpuId = 0;
			oNVBufParam_vsrc.colorFormat = NVBUF_COLOR_FORMAT_UYVY;

			nBuffers_vsrc = 2;
			nBuffers_venc = 2;

			memset(&oNVBufParam_venc, 0, sizeof(oNVBufParam_venc));
			oNVBufParam_venc.layout = NVBUF_LAYOUT_PITCH;
			oNVBufParam_venc.memType = NVBUF_MEM_DEFAULT;
			oNVBufParam_venc.gpuId = 0;
			oNVBufParam_venc.colorFormat = NVBUF_COLOR_FORMAT_GRAY8;

			oBitRate.log_prefix = "vdec";
			oBitRate.Reset();

			switch(1) { case 1:
				oLogLock.clear();
				nVdecCounter.store(0);
				oLog.open("vdec.log");
				_FreeStack_ += [&]() {
					oLog.close();
				};

				nFrames = 0;

				oCodeStream.open("codestream.jsx", std::ios::binary);
				_FreeStack_ += [&]() {
					oCodeStream.close();
				};

				oCodeStreamIdx.open("codestream.idx");
				_FreeStack_ += [&]() {
					oCodeStreamIdx.close();
				};

				int nCodeStreamSize;
				oCodeStreamIdx >> nCodeStreamSize;
				LOGD("nCodeStreamSize=%d", nCodeStreamSize);
				std::string strCodeStreamHeader(nCodeStreamSize, ' ');
				oCodeStream.read(&strCodeStreamHeader[0], nCodeStreamSize);

				oCodeStreamIdx.seekg(0);
				oCodeStream.seekg(0);

				ipxgpucodec_picture_configuration_t* pic_config = NULL;
				ipxerr = ipxgpucodec_dec_create_picture_configuration_from_codestream_headers(
					&strCodeStreamHeader[0], nCodeStreamSize, &pic_config);
				if(ipxerr != IPXGPUCODEC_SUCCESS) {
					LOGE("%s(%d): ipxgpucodec_dec_create_picture_configuration_from_codestream_headers() failed, ipxerr=%d",
						__FUNCTION__, __LINE__, ipxerr);
					break;
				}
				oFreeStack += [&, pic_config]() {
					ipxgpucodec_error_t ipxerr;

					ipxerr = ipxgpucodec_delete_picture_configuration(pic_config);
					if(ipxerr != IPXGPUCODEC_SUCCESS) {
						LOGE("%s(%d): ipxgpucodec_delete_picture_configuration() failed, ipxerr=%d",
							__FUNCTION__, __LINE__, ipxerr);
					}
				};

				uint32_t nAttrSize;

				ipxgpucodec_picture_profile_t pic_profile;
				ipxerr = ipxgpucodec_get_picture_configuration(pic_config,
					IPXGPUCODEC_PIC_PROFILE,
					&pic_profile,
					sizeof(pic_profile),
					&nAttrSize);
				if(ipxerr != IPXGPUCODEC_SUCCESS) {
					LOGE("%s(%d): ipxgpucodec_get_picture_configuration(IPXGPUCODEC_PIC_PROFILE) failed, ipxerr=%d",
						__FUNCTION__, __LINE__, ipxerr);
					break;
				}
				LOGD("pic_profile=%d (%d)", (int)pic_profile, nAttrSize);

				ipxerr = ipxgpucodec_get_picture_configuration(pic_config,
					IPXGPUCODEC_PIC_WIDTH,
					&nWidth,
					sizeof(nWidth),
					&nAttrSize);
				if(ipxerr != IPXGPUCODEC_SUCCESS) {
					LOGE("%s(%d): ipxgpucodec_get_picture_configuration(IPXGPUCODEC_PIC_WIDTH) failed, ipxerr=%d",
						__FUNCTION__, __LINE__, ipxerr);
					break;
				}
				LOGD("nWidth=%d (%d)", nWidth, nAttrSize);

				ipxerr = ipxgpucodec_get_picture_configuration(pic_config,
					IPXGPUCODEC_PIC_HEIGHT,
					&nHeight,
					sizeof(nHeight),
					&nAttrSize);
				if(ipxerr != IPXGPUCODEC_SUCCESS) {
					LOGE("%s(%d): ipxgpucodec_get_picture_configuration(IPXGPUCODEC_PIC_HEIGHT) failed, ipxerr=%d",
						__FUNCTION__, __LINE__, ipxerr);
					break;
				}
				LOGD("nHeight=%d (%d)", nHeight, nAttrSize);

				ipxerr = ipxgpucodec_get_picture_configuration(pic_config,
					IPXGPUCODEC_PIC_CHROMA_FORMAT,
					&nChromaFormat,
					sizeof(nChromaFormat),
					&nAttrSize);
				if(ipxerr != IPXGPUCODEC_SUCCESS) {
					LOGE("%s(%d): ipxgpucodec_get_picture_configuration(IPXGPUCODEC_PIC_CHROMA_FORMAT) failed, ipxerr=%d",
						__FUNCTION__, __LINE__, ipxerr);
					break;
				}
				LOGD("nChromaFormat=%d (%d)", nChromaFormat, nAttrSize);

				ipxerr = ipxgpucodec_get_picture_configuration(pic_config,
					IPXGPUCODEC_PIC_BIT_DEPTH,
					&nBitDepth,
					sizeof(nBitDepth),
					&nAttrSize);
				if(ipxerr != IPXGPUCODEC_SUCCESS) {
					LOGE("%s(%d): ipxgpucodec_get_picture_configuration(IPXGPUCODEC_PIC_BIT_DEPTH) failed, ipxerr=%d",
						__FUNCTION__, __LINE__, ipxerr);
					break;
				}
				LOGD("nBitDepth=%d (%d)", nBitDepth, nAttrSize);

				ipxgpucodec_image_format_configuration_t* fmt_config = NULL;
				ipxerr = ipxgpucodec_create_image_format_configuration(
					IPXGPUCODEC_FMT_UYVY, &fmt_config);
				if(ipxerr != IPXGPUCODEC_SUCCESS) {
					LOGE("%s(%d): ipxgpucodec_create_image_format_configuration() failed, ipxerr=%d",
						__FUNCTION__, __LINE__, ipxerr);
					break;
				}
				oFreeStack += [&, fmt_config]() {
					ipxgpucodec_error_t ipxerr;

					ipxerr = ipxgpucodec_delete_image_format_configuration(fmt_config);
					if(ipxerr != IPXGPUCODEC_SUCCESS) {
						LOGE("%s(%d): ipxgpucodec_delete_image_format_configuration() failed, ipxerr=%d",
							__FUNCTION__, __LINE__, ipxerr);
					}
				};

				// setup decoder configuration
				ipxgpucodec_decoder_configuration_t* dec_config = NULL;
				ipxerr = ipxgpucodec_dec_create_decoder_configuration(&dec_config);
				if(ipxerr != IPXGPUCODEC_SUCCESS) {
					LOGE("%s(%d): ipxgpucodec_dec_create_decoder_configuration() failed, ipxerr=%d",
						__FUNCTION__, __LINE__, ipxerr);
					break;
				}

				ipxgpucodec_dec_input_mode_t input_mode = IPXGPUCODEC_DEC_CUDA_BUFFER_INPUT;
				ipxerr = ipxgpucodec_dec_set_decoder_configuration(dec_config, IPXGPUCODEC_DEC_INPUT_MODE, &input_mode, sizeof(input_mode));
				if(ipxerr != IPXGPUCODEC_SUCCESS) {
					LOGE("%s(%d): ipxgpucodec_dec_set_decoder_configuration(IPXGPUCODEC_DEC_INPUT_MODE) failed, ipxerr=%d",
						__FUNCTION__, __LINE__, ipxerr);
					break;
				}

				ipxgpucodec_dec_output_mode_t output_mode = IPXGPUCODEC_DEC_CUDA_BUFFER_OUTPUT;
				ipxerr = ipxgpucodec_dec_set_decoder_configuration(dec_config, IPXGPUCODEC_DEC_OUTPUT_MODE, &output_mode, sizeof(output_mode));
				if(ipxerr != IPXGPUCODEC_SUCCESS) {
					LOGE("%s(%d): ipxgpucodec_dec_set_decoder_configuration(IPXGPUCODEC_DEC_OUTPUT_MODE) failed, ipxerr=%d",
						__FUNCTION__, __LINE__, ipxerr);
					break;
				}

				ipxgpucodec_bool_t use_primary_cuda_context = IPXGPUCODEC_TRUE;
				ipxerr = ipxgpucodec_dec_set_decoder_configuration(dec_config, IPXGPUCODEC_DEC_INTEROPERATION_PRIMARY_CUDA_CONTEXT, &use_primary_cuda_context, sizeof(use_primary_cuda_context));
				if(ipxerr != IPXGPUCODEC_SUCCESS) {
					LOGE("%s(%d): ipxgpucodec_dec_set_decoder_configuration(IPXGPUCODEC_DEC_INTEROPERATION_PRIMARY_CUDA_CONTEXT) failed, ipxerr=%d",
						__FUNCTION__, __LINE__, ipxerr);
					break;
				}

				LOGD("Create decoder...");
				pDecoder = NULL;
				ipxerr = ipxgpucodec_dec_create_decoder(
					dec_config, pic_config, fmt_config, &pDecoder
				);
				if(ipxerr != IPXGPUCODEC_SUCCESS) {
					LOGE("%s(%d): ipxgpucodec_dec_create_decoder() failed, ipxerr=%d",
						__FUNCTION__, __LINE__, ipxerr);
					break;
				}
				oFreeStack += [&]() {
					ipxgpucodec_error_t ipxerr;

					ipxerr = ipxgpucodec_dec_delete_decoder(pDecoder);
					if(ipxerr != IPXGPUCODEC_SUCCESS) {
						LOGE("%s(%d): ipxgpucodec_dec_delete_decoder() failed, ipxerr=%d",
							__FUNCTION__, __LINE__, ipxerr);
					}
				};

				ipxerr = ipxgpucodec_dec_get_decoder_information(
					pDecoder,
					IPXGPUCODEC_DEC_INPUT_CODESTREAM_SIZE,
					&codestream_size,
					sizeof(codestream_size),
					NULL
				);
				if(ipxerr != IPXGPUCODEC_SUCCESS) {
					LOGE("%s(%d): ipxgpucodec_dec_get_decoder_information(IPXGPUCODEC_DEC_INPUT_CODESTREAM_SIZE) failed, ipxerr=%d",
						__FUNCTION__, __LINE__, ipxerr);
					break;
				}
				LOGD("codestream_size=%d", codestream_size);

				ipxerr = ipxgpucodec_dec_get_decoder_information(
					pDecoder,
					IPXGPUCODEC_DEC_OUTPUT_PACKED_IMAGE_SIZE,
					&packed_image_size,
					sizeof(packed_image_size),
					NULL
				);
				if(ipxerr != IPXGPUCODEC_SUCCESS) {
					LOGE("%s(%d): ipxgpucodec_dec_get_decoder_information(IPXGPUCODEC_DEC_OUTPUT_PACKED_IMAGE_SIZE) failed, ipxerr=%d",
						__FUNCTION__, __LINE__, ipxerr);
					break;
				}
				LOGD("packed_image_size=%d", packed_image_size);

				oNVBufParam_vsrc.width = (packed_image_size / nHeight) / 2;
				oNVBufParam_vsrc.height = nHeight;
				LOGD("oNVBufParam_vsrc=%dx%d", oNVBufParam_vsrc.width, oNVBufParam_vsrc.height);

				oNVBufParam_venc.width = codestream_size / nHeight;
				oNVBufParam_venc.height = nHeight;
				LOGD("oNVBufParam_venc=%dx%d", oNVBufParam_venc.width, oNVBufParam_venc.height);

				for(int i = 0;i < nBuffers_vsrc;i++) {
					qcap2_rcbuffer_t* pRCBuffer = new_mapped_nvbuf(oFreeStack, oNVBufParam_vsrc,
						NVBUF_MAP_READ_WRITE, CU_GRAPHICS_MAP_RESOURCE_FLAGS_NONE);

					oRCBuffers_vsrc.push_back(pRCBuffer);
				}
				oFreeStack += [&]() {
					oRCBuffers_vsrc.clear();
				};

				pRCBufferQ_vsrc = qcap2_rcbuffer_queue_new();
				oFreeStack += [&]() {
					qcap2_rcbuffer_queue_delete(pRCBufferQ_vsrc);
				};

				qcap2_rcbuffer_queue_set_max_buffers(pRCBufferQ_vsrc, nBuffers_vsrc);
				qcap2_rcbuffer_queue_set_buffers(pRCBufferQ_vsrc, &oRCBuffers_vsrc[0]);

				qres = qcap2_rcbuffer_queue_start(pRCBufferQ_vsrc);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_rcbuffer_queue_start() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
				oFreeStack += [&]() {
					QRESULT qres;

					LOGD("%s(%d):++++", __FUNCTION__, __LINE__);
					qres = qcap2_rcbuffer_queue_stop(pRCBufferQ_vsrc);
					LOGD("%s(%d):----", __FUNCTION__, __LINE__);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): qcap2_rcbuffer_queue_stop() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					}
				};

				for(int i = 0;i < nBuffers_venc;i++) {
					qcap2_rcbuffer_t* pRCBuffer = new_mapped_nvbuf(oFreeStack, oNVBufParam_venc,
						NVBUF_MAP_READ_WRITE, CU_GRAPHICS_MAP_RESOURCE_FLAGS_NONE);

#if 1
					std::shared_ptr<qcap2_av_frame_t> pAVFrame(
						(qcap2_av_frame_t*)qcap2_rcbuffer_lock_data(pRCBuffer),
						[pRCBuffer](qcap2_av_frame_t*) {
							qcap2_rcbuffer_unlock_data(pRCBuffer);
						});

					uint8_t* pData[4];
					int pLineSize[4];
					qcap2_av_frame_get_buffer1(pAVFrame.get(), pData, pLineSize);

					memcpy(pData[0], &strCodeStreamHeader[0], codestream_size);
					qcap2_rcbuffer_sync_nvbuf_for_device(pRCBuffer);
#endif

					oRCBuffers_venc.push_back(pRCBuffer);
				}
				oFreeStack += [&]() {
					oRCBuffers_venc.clear();
				};

				pRCBufferQ_venc = qcap2_rcbuffer_queue_new();
				oFreeStack += [&]() {
					qcap2_rcbuffer_queue_delete(pRCBufferQ_venc);
				};

				qcap2_rcbuffer_queue_set_max_buffers(pRCBufferQ_venc, nBuffers_venc);
				qcap2_rcbuffer_queue_set_buffers(pRCBufferQ_venc, &oRCBuffers_venc[0]);

				qres = qcap2_rcbuffer_queue_start(pRCBufferQ_venc);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_rcbuffer_queue_start() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}
				oFreeStack += [&]() {
					QRESULT qres;

					LOGD("%s(%d):++++", __FUNCTION__, __LINE__);
					qres = qcap2_rcbuffer_queue_stop(pRCBufferQ_venc);
					LOGD("%s(%d):----", __FUNCTION__, __LINE__);
					if(qres != QCAP_RS_SUCCESSFUL) {
						LOGE("%s(%d): qcap2_rcbuffer_queue_stop() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					}
				};

#if 1
				bPullerRunning.store(true);
				std::thread t1(std::bind(&self_t::Main_Puller, this));

				wait_for_test_finish([&]() {
					DoVdec();
				}, 1000000LL, 60LL);

				LOGD("Wait for pusher ++++");
				ipxerr = ipxgpucodec_dec_set_end_of_codestream(pDecoder, true);
				if(ipxerr != IPXGPUCODEC_SUCCESS) {
					LOGE("%s(%d): ipxgpucodec_dec_set_end_of_codestream() failed, ipxerr=%d",
						__FUNCTION__, __LINE__, ipxerr);
					break;
				}

				bPullerRunning.store(false);
				t1.join();
				LOGD("Wait for pusher ----");
#endif
			}

			oFreeStack.flush();
		}

		void Main_Puller() {
			cudaFree(NULL);

			LOGD("%s(%d): ++++", __FUNCTION__, __LINE__);

			while(bPullerRunning.load()) {
				DoVsrc();
			}

			LOGD("%s(%d): ----", __FUNCTION__, __LINE__);
		}

		int DoVdec() {
			QRESULT qres;
			ipxgpucodec_error_t ipxerr;
			int nTasks = 0;
			int64_t now;

			switch(1) { case 1:
				qcap2_rcbuffer_t* pRCBuffer;
				qres = qcap2_rcbuffer_queue_pop(pRCBufferQ_venc, &pRCBuffer);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_rcbuffer_queue_pop() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				now = _clk();

				nTasks++;
				std::shared_ptr<qcap2_rcbuffer_t> pRCBuffer_(pRCBuffer, qcap2_rcbuffer_release);
				std::shared_ptr<qcap2_av_frame_t> pAVFrame(
					(qcap2_av_frame_t*)qcap2_rcbuffer_lock_data(pRCBuffer),
					[pRCBuffer](qcap2_av_frame_t*) {
						qcap2_rcbuffer_unlock_data(pRCBuffer);
					});

				uintptr_t pBufferSrc[3];
				int pPitchSrc[3];
				qres = qcap2_rcbuffer_get_mapped_eglbuffer(pRCBuffer, pBufferSrc, pPitchSrc);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("qcap2_rcbuffer_get_mapped_eglbuffer(), qres=%d", qres);
					break;
				}

#if 0
				LOGD("(%p, %p, %p), (%d, %d, %d)",
					pBufferSrc[0], pBufferSrc[1], pBufferSrc[2],
					pPitchSrc[0], pPitchSrc[1], pPitchSrc[2]);
#endif
				{
					int val = nVdecCounter.fetch_add(1);

					char buf[256];
					ssize_t count = sprintf(buf, "vdec[%.2f]: ++++ %d\n", now / 1000.0, val);

					spinlock_lock(oLogLock);
					oLog.write(buf, count);
					spinlock_unlock(oLogLock);
				}

				ipxerr = ipxgpucodec_dec_push_frame(pDecoder,
					(const void*)pBufferSrc[0], codestream_size);
				if(ipxerr != IPXGPUCODEC_SUCCESS) {
					LOGE("%s(%d): ipxgpucodec_dec_push_frame() failed, ipxerr=%d",
						__FUNCTION__, __LINE__, ipxerr);
					break;
				}
			}

			return nTasks;
		}

		int DoVsrc() {
			QRESULT qres;
			ipxgpucodec_error_t ipxerr;
			int nTasks = 0;
			int64_t now;
			uint8_t* pData;

			switch(1) { case 1:
				qcap2_rcbuffer_t* pRCBuffer;
				qres = qcap2_rcbuffer_queue_pop(pRCBufferQ_vsrc, &pRCBuffer);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("%s(%d): qcap2_rcbuffer_queue_pop() failed, qres=%d", __FUNCTION__, __LINE__, qres);
					break;
				}

				now = _clk();

				nTasks++;
				std::shared_ptr<qcap2_rcbuffer_t> pRCBuffer_(pRCBuffer, qcap2_rcbuffer_release);
				std::shared_ptr<qcap2_av_frame_t> pAVFrame(
					(qcap2_av_frame_t*)qcap2_rcbuffer_lock_data(pRCBuffer),
					[pRCBuffer](qcap2_av_frame_t*) {
						qcap2_rcbuffer_unlock_data(pRCBuffer);
					});

				uintptr_t pBufferSrc[3];
				int pPitchSrc[3];
				qres = qcap2_rcbuffer_get_mapped_eglbuffer(pRCBuffer, pBufferSrc, pPitchSrc);
				if(qres != QCAP_RS_SUCCESSFUL) {
					LOGE("qcap2_rcbuffer_get_mapped_eglbuffer(), qres=%d", qres);
					break;
				}

				ipxerr = ipxgpucodec_dec_pull_frame(pDecoder,
					(void*)pBufferSrc[0], packed_image_size);
				if(ipxerr != IPXGPUCODEC_SUCCESS) {
					if(ipxerr == IPXGPUCODEC_ERR_END_OF_CODESTREAM)
						break;

					LOGE("%s(%d): ipxgpucodec_dec_pull_frame() failed, ipxerr=%d",
						__FUNCTION__, __LINE__, ipxerr);
					break;
				}

#if 0
				qcap2_rcbuffer_sync_nvbuf_for_cpu(pRCBuffer);
#endif

				if(nFrames == 0) {
					qcap2_save_raw_video_frame(pRCBuffer, "vdec");
				}

				nFrames++;

				now = _clk();
				oBitRate.Log(packed_image_size * 8, now);

				{
					int val = nVdecCounter.fetch_sub(1);

					char buf[256];
					ssize_t count = sprintf(buf, "vdec[%.2f]: ---- %d\n", now / 1000.0, val);

					spinlock_lock(oLogLock);
					oLog.write(buf, count);
					spinlock_unlock(oLogLock);
				}

				nTasks++;
			}

			return nTasks;
		}
	} mTestCase3;
#endif
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
