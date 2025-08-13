#include "ZzUtils.h"
#include "ZzLog.h"

#include <fstream>
#include <sys/timerfd.h>

ZZ_INIT_LOG("ZzUtils");

namespace __zz_utils__ {
	void _init_() {
	}

	void _uninit_() {
	}
}

namespace ZzUtils {
	void uio_enable_irq(int fd, bool enable) {
		int err;

		uint32_t tmp = enable ? 1 : 0;
		err = write(fd, &tmp, sizeof(uint32_t));
		if(err == 0) {
			err = errno;
			LOGE("%s(%d): write(), err=%d", __FUNCTION__, __LINE__, err);
		}
	}

	void SetNextTimer(int fd_timer, int64_t duration) {
		int err;

#if 0
		LOGD("fd_timer=%d duration=%.2f", fd_timer, duration / 1000.0);
#endif

		itimerspec timer_spec;
		timer_spec.it_value.tv_sec = duration / 1000000LL;
		timer_spec.it_value.tv_nsec = (duration % 1000000LL) * 1000LL;
		timer_spec.it_interval.tv_sec = 0;
		timer_spec.it_interval.tv_nsec = 0;
		err = timerfd_settime(fd_timer, 0, &timer_spec, NULL);
		if(err == -1) {
			err = errno;
			LOGE("%s(%d): timerfd_settime failed, err = %d", __FUNCTION__, __LINE__, err);
		}
	}

	bool ReadAllBytes(const char* path, std::string& data) {
		std::ifstream ifs(path, std::ios::binary);
		if(! ifs)
			return false;

		std::stringstream buffer;
		buffer << ifs.rdbuf();

		data = buffer.str();

		return true;
	}

	void rgb_yuv_p(int rgb[], int yuv[]) {
		double R = (double)rgb[0] / 255;
		double G = (double)rgb[1] / 255;
		double B = (double)rgb[2] / 255;

		double Y, U, V;

		Y =  0.299 * R + 0.587 * G + 0.114 * B;
		U = -0.169 * R - 0.331 * G + 0.500 * B + 0.5;
		V =  0.500 * R - 0.419 * G - 0.081 * B + 0.5;

		// LOGD("%.2f,%.2f,%.2f ==> %.2f,%.2f,%.2f", R, G, B, Y, Cr, Cb);

		yuv[0] = clamp_255(Y * 255);
		yuv[1] = clamp_255(U * 255);
		yuv[2] = clamp_255(V * 255);
	}

	void yuv_full_limited_p(int yuv_full[], int yuv_limited[]) {
		// Y: 16–235
		// U/V: 16–240

		yuv_limited[0] = 16 + (yuv_full[0] * (235 - 16) / 255);
		yuv_limited[1] = 16 + (yuv_full[1] * (240 - 16) / 255);
		yuv_limited[2] = 16 + (yuv_full[2] * (240 - 16) / 255);
	}

	void yuv_limited_full_p(int yuv_limited[], int yuv_full[]) {
		// Y: 16–235
		// U/V: 16–240

		yuv_full[0] = (yuv_limited[0] - 16) * 255 / (235 - 16);
		yuv_full[1] = (yuv_limited[1] - 16) * 255 / (240 - 16);
		yuv_full[2] = (yuv_limited[2] - 16) * 255 / (240 - 16);
	}

	uint32_t rgb_yuv(uint32_t nColor) {
		uint8_t* pColor = (uint8_t*)&nColor;
		int rgb[] = { pColor[2], pColor[1], pColor[0] };
		int yuv[3];
		rgb_yuv_p(rgb, yuv);
		return (uint32_t)(yuv[2] << 16) | (uint32_t)(yuv[1] << 8) | (uint32_t)yuv[0];
	}

	void rgb_gbr_p(int rgb[], int gbr[]) {
		gbr[0] = rgb[1];
		gbr[1] = rgb[2];
		gbr[2] = rgb[0];
	}

	uint32_t rgb_gbr(uint32_t nColor) {
		uint8_t* pColor = (uint8_t*)&nColor;
		int rgb[] = { pColor[2], pColor[1], pColor[0] };
		int gbr[3];
		rgb_gbr_p(rgb, gbr);
		return (uint32_t)(gbr[2] << 16) | (uint32_t)(gbr[1] << 8) | (uint32_t)gbr[0];
	}

	uint32_t rgb_p(int rgb[]) {
		return (uint32_t)(rgb[0] << 16) | (uint32_t)(rgb[1] << 8) | (uint32_t)rgb[2];
	}

	void Set_8u_P4R(uint8_t* pDst[4], int dstStep[4], int nWidth, int nHeight, uint8_t pValue[4]) {
		for(int i = 0;i < 4;i++) {
			uint8_t* pDst0 = pDst[i];
			for(int y = 0;y < nHeight;y++, pDst0 += dstStep[i]) {
				memset(pDst0, pValue[i], nWidth);
			}
		}
	}

	int NextId() {
		static boost::atomic<int> id(0);

		return ++id;
	}

	Epolls::Epolls() {
		int err;

		fd_epoll = epoll_create(1);
		if(fd_epoll == -1) {
			err = errno;
			LOGE("%s(%d): epoll_create failed, err = %d", __FUNCTION__, __LINE__, err);
		}
	}

	Epolls::~Epolls() {
		if(fd_epoll != -1) {
			close(fd_epoll);
			fd_epoll = -1;
		}
	}

	std::size_t hash(const char* s)
	{
		std::size_t seed = 0;
		while(*s) {
			boost::hash_combine(seed, *s++);
		}

		return seed;
	}

	FreeStack::FreeStack() {
	}

	FreeStack::~FreeStack() {
		if(! empty()) {
			LOGE("%s(%d): unexpected value, size()=%u", __FUNCTION__, __LINE__, size());
		}
	}

	void FreeStack::Flush() {
		while(! empty()) {
			top()();
			pop();
		}
	}
}
