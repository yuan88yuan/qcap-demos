# Temporary PCM write observer

`sc6f0-pcm-write-trace.c` is an opt-in LD_PRELOAD diagnostic, not a playback fix.
It forwards `snd_pcm_writei` / `snd_pcm_prepare` exactly once with unchanged
arguments and preserves the return value and errno. It reports cumulative
process-wide frame and error counters at most once every five seconds. Use one
playback PCM per process. Logging/atomic bookkeeping can affect timing; compare
with an uninstrumented run before drawing conclusions.

Build the shared object inside `qcap-build:xlnk2_arm64-base`, source
`/opt/qcap-dev-init`, then use the SDK's `$CC`:

```sh
$CC -shared -fPIC -O2 -Wall -Wextra -Werror \
  src/tests/sc6f0-pcm-write-trace.c -o /path/to/pcm-write-trace.so -ldl
```

Load only into the receiver executable (after `ip netns exec` / `env`), not
globally into systemd or namespace helpers that do not link ALSA. Symbol
resolution failure intentionally refuses to launch rather than fake success.
Do not overwrite the running binary or change PCM parameters for this test.
On the board use a volatile service override, preserve existing launch arguments
and library paths, and coordinate the required receiver stop/start. Remove only
the diagnostic override afterward and restart to restore the original setup.

Mock transparency tests inside the builder:

```sh
./scripts/docker-run.sh qcap-build:xlnk2_arm64-base \
  python3 src/tests/test-sc6f0-pcm-write-trace.py
```

Compute interval deltas, not raw cumulative counts. `requested - accepted`
includes all failed/partial requests; whether these frames were retried or lost
depends on caller code. EPIPE means XRUN; EAGAIN means nonblocking backpressure,
not a broken device. Prepare counts also include initialization, so use deltas.
Do not equate DEP overrun counters to ALSA XRUNs or LV7300 error counts.
