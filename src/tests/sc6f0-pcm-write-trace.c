/* Diagnostic LD_PRELOAD observer only; never retries or changes PCM data.
 * Compile with the platform SDK: $CC -shared -fPIC -O2 -Wall -Wextra -Werror
 *   sc6f0-pcm-write-trace.c -o pcm-write-trace.so -ldl
 * Enable for one receiver process using LD_PRELOAD=/absolute/path/to/the.so.
 * Totals are process-wide. Use with one playback PCM; logging can affect timing.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

typedef struct _snd_pcm snd_pcm_t;
typedef long snd_pcm_sframes_t;
typedef unsigned long snd_pcm_uframes_t;
static snd_pcm_sframes_t (*next_writei)(snd_pcm_t *, const void *, snd_pcm_uframes_t);
static int (*next_prepare)(snd_pcm_t *);
static _Atomic uint64_t calls, requested, accepted, again, pipe_errors;
static _Atomic uint64_t suspended, short_writes, zero, other_errors;
static _Atomic uint64_t prepares, prepare_errors, last_report_ns;

__attribute__((constructor)) static void resolve_pcm(void)
{
    next_writei = dlsym(RTLD_NEXT, "snd_pcm_writei");
    next_prepare = dlsym(RTLD_NEXT, "snd_pcm_prepare");
    if (!next_writei || !next_prepare) {
        static const char msg[] = "[PCM-TRACE] cannot resolve ALSA symbols\n";
        ssize_t ignored = write(STDERR_FILENO, msg, sizeof(msg) - 1);
        (void)ignored;
        _exit(127);
    }
}

static void report(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts)) return;
    uint64_t now = (uint64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
    uint64_t old = atomic_load(&last_report_ns);
    if (now - old < 5000000000ULL ||
        !atomic_compare_exchange_strong(&last_report_ns, &old, now)) return;
    char line[512];
    int len = snprintf(line, sizeof(line),
        "[PCM-TRACE] uptime_ns=%llu calls=%llu requested=%llu accepted=%llu "
        "eagain=%llu epipe=%llu estrpipe=%llu short=%llu zero=%llu other=%llu "
        "prepare=%llu prepare_errors=%llu\n",
        (unsigned long long)now, (unsigned long long)atomic_load(&calls),
        (unsigned long long)atomic_load(&requested),
        (unsigned long long)atomic_load(&accepted),
        (unsigned long long)atomic_load(&again),
        (unsigned long long)atomic_load(&pipe_errors),
        (unsigned long long)atomic_load(&suspended),
        (unsigned long long)atomic_load(&short_writes),
        (unsigned long long)atomic_load(&zero),
        (unsigned long long)atomic_load(&other_errors),
        (unsigned long long)atomic_load(&prepares),
        (unsigned long long)atomic_load(&prepare_errors));
    if (len > 0 && (size_t)len < sizeof(line)) {
        ssize_t ignored = write(STDERR_FILENO, line, (size_t)len);
        (void)ignored;
    }
}

snd_pcm_sframes_t snd_pcm_writei(snd_pcm_t *pcm, const void *data,
                                snd_pcm_uframes_t frames)
{
    snd_pcm_sframes_t result = next_writei(pcm, data, frames);
    int saved_errno = errno;
    atomic_fetch_add(&calls, 1);
    atomic_fetch_add(&requested, frames);
    if (result > 0) {
        atomic_fetch_add(&accepted, (uint64_t)result);
        if ((snd_pcm_uframes_t)result < frames) atomic_fetch_add(&short_writes, 1);
    } else if (!result) atomic_fetch_add(&zero, 1);
    else if (result == -EAGAIN) atomic_fetch_add(&again, 1);
    else if (result == -EPIPE) atomic_fetch_add(&pipe_errors, 1);
    else if (result == -ESTRPIPE) atomic_fetch_add(&suspended, 1);
    else atomic_fetch_add(&other_errors, 1);
    report();
    errno = saved_errno;
    return result;
}

int snd_pcm_prepare(snd_pcm_t *pcm)
{
    int result = next_prepare(pcm);
    int saved_errno = errno;
    atomic_fetch_add(&prepares, 1);
    if (result < 0) atomic_fetch_add(&prepare_errors, 1);
    errno = saved_errno;
    return result;
}
