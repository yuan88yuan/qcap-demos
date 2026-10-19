#pragma once

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace sc6f0_pcm {
struct write_stats {
    uint64_t accepted = 0, again = 0, short_writes = 0, xruns = 0;
    uint64_t suspended = 0, errors = 0, recovery_errors = 0, dropped = 0;
};

// Called under the owner's audio mutex. Never wait/spin on backpressure and
// never resubmit accepted frames. Only an explicit bounded-queue overflow drops
// frames; callers must report that loss. Stereo S24_LE occupies 8 bytes/frame.
template<class Write, class Prepare>
write_stats drain(std::vector<uint8_t>& pending, bool& short_tail,
                  Write write, Prepare prepare) {
    constexpr size_t frame_bytes = 8, chunk = 960, max_frames = 7680;
    write_stats stats;
    if (pending.size() > max_frames * frame_bytes) {
        size_t excess = pending.size() - max_frames * frame_bytes;
        pending.erase(pending.begin(), pending.begin() + excess);
        stats.dropped = excess / frame_bytes;
        short_tail = false;
    }
    for (unsigned attempts = 0; attempts < 16; ++attempts) {
        size_t available = pending.size() / frame_bytes;
        if (!available || (!short_tail && available < chunk)) break;
        size_t requested = std::min(available, chunk);
        long written = write(pending.data(), requested);
        if (written > 0 && static_cast<size_t>(written) <= requested) {
            pending.erase(pending.begin(), pending.begin() + written * frame_bytes);
            stats.accepted += written;
            short_tail = static_cast<size_t>(written) < requested;
            if (short_tail) ++stats.short_writes;
            continue;
        }
        if (!written || written == -EAGAIN || written == -EINTR) {
            if (written == -EAGAIN) ++stats.again;
            break; // Retain all unaccepted data; retry on the next input callback.
        }
        ++stats.errors;
        if (written == -EPIPE || written == -ESTRPIPE) {
            if (written == -EPIPE) ++stats.xruns;
            else ++stats.suspended;
            if (prepare() < 0) ++stats.recovery_errors;
        }
        break; // Bounded recovery, not a tight retry loop while holding a mutex.
    }
    return stats;
}
} // namespace sc6f0_pcm
