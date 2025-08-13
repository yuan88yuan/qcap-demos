#ifndef __ZZ_CLOCK_H__
#define __ZZ_CLOCK_H__

#include <stdint.h>

struct ZzClock {
	explicit ZzClock();
	~ZzClock();

	int64_t operator()();
};

extern int qcap_gettimeofday(struct timeval *tv, struct timezone *tz);

namespace __zz_clock__ {
	extern ZzClock _clk;
};

#endif // __ZZ_CLOCK_H__