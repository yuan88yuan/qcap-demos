#pragma once

#include <stdint.h>
#include <string.h>

namespace sc6f0_sdi_resync {
// TRANSITIONAL WORKAROUND, not a root-cause fix or picture/current-lock detector.
// The App cannot receive FS ON/OFF events. This register-only heuristic matches
// the captured ABI3 4K60 fault; benign history can trigger it and other lags can
// be missed. Replace/remove it once downstream hardware resynchronization is fixed.
constexpr uint64_t kSampleMs = 500;
constexpr uint64_t kDebounceMs = 2000;
constexpr uint64_t kCooldownMs = 5000;
constexpr uint64_t kAttemptWindowMs = 60000;
constexpr unsigned kMaxAttempts = 3;

inline bool known_fpga(const char* version) {
	// Native SDI register layout retained when diagnostic ABI3 taps are removed.
	return version && (!strncmp(version, "0x26102101 SDIR", 15) ||
	                   !strncmp(version, "0x26102201 SDIR", 15));
}

inline bool has_suspect_alignment_history(uint32_t status) {
	// 0xB00C006C bit11 is sticky lost-align history, not live picture state.
	// High16 lag12 matches observed ON faults; observed OFF high lags are excluded.
	return status != UINT32_MAX && (status & (1u << 11)) && (status >> 16) == 12;
}

struct Policy {
	uint64_t next_sample_ms{0}, suspect_ms{0}, cooldown_until_ms{0};
	uint64_t attempts_ms[kMaxAttempts]{};
	unsigned samples{0}, attempts{0};
	bool failed{false};

	bool observe(uint64_t now, uint32_t status, bool eligible) {
		if (now < next_sample_ms) return false;
		// A delayed main loop is not evidence of continuously bad samples.
		if (next_sample_ms && now > next_sample_ms + 2 * kSampleMs) samples = 0;
		next_sample_ms = now + kSampleMs;
		const bool suspect = eligible && has_suspect_alignment_history(status);
		if (failed || !suspect || now < cooldown_until_ms) {
			samples = 0;
			return false;
		}
		if (!samples++) suspect_ms = now;
		if (samples < 5 || now - suspect_ms < kDebounceMs) return false;
		unsigned recent = 0;
		for (unsigned i = 0; i < attempts && i < kMaxAttempts; ++i)
			if (now - attempts_ms[i] < kAttemptWindowMs) ++recent;
		if (recent >= kMaxAttempts) return false;
		attempts_ms[attempts % kMaxAttempts] = now;
		++attempts;
		cooldown_until_ms = now + kCooldownMs;
		samples = 0;
		return true;
	}
};
}
