#!/usr/bin/env python3
"""Host mock test: preload observation preserves ALSA results and errno.

Run inside the builder image with python3; requires native cc, no hardware.
"""
import os
from pathlib import Path
import subprocess
import tempfile

source = Path(__file__).with_name('sc6f0-pcm-write-trace.c').resolve()
with tempfile.TemporaryDirectory(prefix='pcm-trace-') as tmp:
    root = Path(tmp)
    (root / 'mock.c').write_text(r'''
#include <errno.h>
#include <stdlib.h>
long snd_pcm_writei(void *pcm, const void *data, unsigned long frames) {
    if (pcm != (void *)1 || data != (void *)2 || frames != 960) abort();
    errno = EDOM;
    return strtol(getenv("RESULT"), 0, 10);
}
int snd_pcm_prepare(void *pcm) {
    if (pcm != (void *)1) abort();
    errno = ERANGE;
    return -EIO;
}
''')
    (root / 'main.c').write_text(r'''
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
extern long snd_pcm_writei(void *, const void *, unsigned long);
extern int snd_pcm_prepare(void *);
int main(void) {
    long result = strtol(getenv("RESULT"), 0, 10);
    assert(snd_pcm_prepare((void *)1) == -EIO);
    assert(errno == ERANGE);
    assert(snd_pcm_writei((void *)1, (void *)2, 960) == result);
    assert(errno == EDOM);
    return 0;
}
''')
    flags = ['cc', '-Wall', '-Wextra', '-Werror']
    subprocess.run(flags + ['-shared', '-fPIC', str(root / 'mock.c'),
                            '-o', str(root / 'libmock.so')], check=True)
    subprocess.run(flags + ['-shared', '-fPIC', str(source), '-ldl',
                            '-o', str(root / 'trace.so')], check=True)
    subprocess.run(flags + [str(root / 'main.c'), '-L' + tmp, '-lmock',
                            '-Wl,-rpath,' + tmp, '-o', str(root / 'main')], check=True)
    for result, expected in [(960, 'accepted=960'), (240, 'short=1'),
                             (0, 'zero=1'), (-11, 'eagain=1'),
                             (-32, 'epipe=1'), (-86, 'estrpipe=1'),
                             (-5, 'other=1')]:
        env = dict(os.environ, LD_PRELOAD=str(root / 'trace.so'), RESULT=str(result))
        run = subprocess.run([str(root / 'main')], env=env,
                             capture_output=True, text=True, check=True)
        assert expected in run.stderr, run.stderr
        assert 'prepare=1 prepare_errors=1' in run.stderr, run.stderr
    print('PASS: 7 return paths, argument/errno preservation and prepare counters')
