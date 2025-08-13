#include "ZzClock.h"
#include "ZzLog.h"
#include <time.h>
#include <sys/time.h>

#if 0
#if BUILD_HISIV
#include <mpi_sys.h>
#elif BUILD_LINUX
#include <time.h>
#else
#error "BUILD_TARGET is missing"
#endif
#endif

ZZ_INIT_LOG("ZzClock");

namespace __zz_clock__ {
	ZzClock _clk;

	inline int64_t GetTime() {
		int64_t ret;

#if 0
#if BUILD_HI3531A || BUILD_HI3531D

		HI_U64 curPts;
		HI_MPI_SYS_GetCurPts(&curPts);
		ret = int64_t(curPts);

#endif

#if BUILD_HI3559A || BUILD_HI3519A

		HI_U64 curPts;
		HI_MPI_SYS_GetCurPTS(&curPts);
		ret = int64_t(curPts);

#endif

#if BUILD_LINUX

		struct timespec cur_time;
		clock_gettime(CLOCK_REALTIME, &cur_time);

		ret = (int64_t)cur_time.tv_sec * 1000000LL + (int64_t)cur_time.tv_nsec / 1000LL;

#endif
#else
		struct timespec cur_time;
		clock_gettime(CLOCK_MONOTONIC, &cur_time);

		ret = (int64_t)cur_time.tv_sec * 1000000LL + (int64_t)cur_time.tv_nsec / 1000LL;
#endif

		return ret;
	}
}

using namespace __zz_clock__;

ZzClock::ZzClock() {

}

ZzClock::~ZzClock() {

}

int64_t ZzClock::operator()() {
	return GetTime();
}

int qcap_gettimeofday(struct timeval *tv, struct timezone *tz) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);

	TIMESPEC_TO_TIMEVAL(tv, &ts);

	return 0;
}
