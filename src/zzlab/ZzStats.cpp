#include "ZzStats.h"
#include "ZzLog.h"

#include <fcntl.h>
#include <sys/shm.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include "ZzUtils.h"

ZZ_INIT_LOG("Stats");

namespace __zz_stats__ {
	bool QCAP_FORCE_SHOW_FPS;

	void _init_() {
		ZZ_ENV_INIT(QCAP_FORCE_SHOW_FPS, false);
#if 0
		if(QCAP_FORCE_SHOW_FPS) {
			LOGW("QCAP_FORCE_SHOW_FPS=true");
		} else {
			LOGW("QCAP_FORCE_SHOW_FPS=false");
		}
#endif
	}

	void _uninit_() {
	}
}

ZzStatBitRate::ZzStatBitRate() {
	stats_duration = 1000000LL;
	sh_mempos = 0;
	shm_id = -1;
}

void ZzStatBitRate::Reset() {
	last_ts = 0;
	cur_ts = 0;
	acc_ticks = 0;
	acc_bits = 0;
	max_bits = 0;

	if (shm_id >= 0 && shm_ptr) {
		LOGW("%s(%d): Release", __FUNCTION__, __LINE__);
		double fps = 0.0;
		memcpy(shm_ptr, &fps, sizeof(double));
		shmdt(shm_ptr);

		sh_mempos = 0;
		shm_id = -1;
	}
}

void ZzStatBitRate::Share(int memup) {
	sh_mempos = memup;

	shm_id = shmget(sh_mempos, 0x000010, IPC_CREAT | 0666);
	if(shm_id < 0) {
		LOGE("%s(%d): Failed to mount shmget", __FUNCTION__, __LINE__);
	}

	shm_ptr = (char *)shmat(shm_id, NULL, 0);
}

bool ZzStatBitRate::Log(int64_t bits, int64_t ts) {
	acc_bits += bits;

	if(bits > max_bits) {
		max_bits = bits;
	}

	++acc_ticks;

	int64_t duration = ts - last_ts;
	if(duration > stats_duration) {
		const char* units_name = "bps";
		double factor_den;
		if(acc_bits < 1024LL * 1024LL) {
			units_name = "Kibps";
			factor_den = 1024.0;
		} else if(acc_bits < 1024LL * 1024LL * 1024LL) {
			units_name = "Mibps";
			factor_den = (1024.0 * 1024.0);
		} else {
			units_name = "Gibps";
			factor_den = (1024.0 * 1024.0 * 1024.0);
		}
		double freq = 1000000.0 / duration;

		if (shm_id >= 0 && shm_ptr && (__zz_stats__::QCAP_FORCE_SHOW_FPS == false)) {
			double fps = acc_ticks * freq;
			memcpy(shm_ptr, &fps, sizeof(double));
			memcpy(shm_ptr + sizeof(double), &ts, sizeof(int64_t));
		} else {
			LOGD("%s: %.2fFPS, %.2f%s, max %.2fKibits .", log_prefix.c_str(), acc_ticks * freq,
				(acc_bits * freq) / factor_den, units_name, max_bits / 1024.0);
		}

		last_ts = ts;
		cur_ts = 0;
		acc_ticks = 0;
		acc_bits = 0;

		return true;
	}

	return false;
}

ZzStatCounter::ZzStatCounter() {
	stats_duration = 1000000LL;
}

void ZzStatCounter::Reset() {
	last_ts = 0;
	cur_ts = 0;

	acc_ticks = 0;
	acc_counter = 0;
}

bool ZzStatCounter::Log(int64_t cur_counter, int64_t ts) {
	acc_counter += cur_counter;
	++acc_ticks;

	int64_t duration = ts - last_ts;
	if(duration > stats_duration) {
		LOGD("%s: %.2fFPS, AVG=%.2f (%.2f%%), CNT=%lld", log_prefix.c_str(), acc_ticks * 1000000.0 / duration,
			(double)acc_counter / acc_ticks, acc_counter * 100.0 / acc_ticks, acc_counter);

		last_ts = ts;
		cur_ts = 0;
		acc_ticks = 0;
		acc_counter = 0;

		return true;
	}

	return false;
}

ZzStatEvents::ZzStatEvents() {
	stats_duration = 1000000LL;
}

void ZzStatEvents::Reset() {
	events = 0;
	last_ts = 0;
}

void ZzStatEvents::Log(int64_t ts) {
	if(ts - last_ts >= stats_duration) {
		LOGD("%s: events=%d (%.2f EvtPerSec)", log_prefix.c_str(),
			events, events * 1000000.0 / (ts - last_ts));

		events = 0;
		last_ts = ts;
	}

	events++;
}
