#ifndef __ZZ_STATS_H__
#define __ZZ_STATS_H__

#include <string>
#include <stdint.h>

struct ZzStatBitRate {
	std::string log_prefix;

	int64_t stats_duration;
	int64_t cur_ts;
	int64_t last_ts;

	int acc_ticks;
	int64_t acc_bits;

	int64_t max_bits;

	int sh_mempos;
	int shm_id;
	char *shm_ptr;

	ZzStatBitRate();

	void Reset();
	void Share(int memup);
	bool Log(int64_t bits, int64_t ts);
};

struct ZzStatCounter {
	std::string log_prefix;

	int64_t stats_duration;
	int64_t cur_ts;
	int64_t last_ts;
	int acc_ticks;
	int64_t acc_counter;

	ZzStatCounter();

	void Reset();
	bool Log(int64_t cur_counter, int64_t ts);
};

struct ZzStatEvents {
	std::string log_prefix;

	int64_t stats_duration;

	int events;
	int64_t last_ts;

	ZzStatEvents();

	void Reset();
	void Log(int64_t ts);
};

#endif // __ZZ_STATS_H__
