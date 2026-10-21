#!/usr/bin/env python3
"""Host regression of the actual unchanged-format DAU fast path, not HW."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
app = (root / 'src/tests/sc6f0-dante-rx.cpp').read_text()
start = app.index('\t\t// SDI unchanged locked-format fast path:')
end = app.index('\t\t// End unchanged locked-format fast path.', start)
branch = app[start:end]
harness = r'''
#include <cassert>
#include <cmath>
#include <cstdint>
#define LOGI(...) ((void)0)
static bool sdi = true;
static bool output_is_sdi() { return sdi; }
struct Mode { unsigned hdisplay=3840, vdisplay=2160; double fps=60; };
struct Context { bool modeset_active=true, overlay_active=true; Mode current_mode; };
struct Format { bool locked=true; };
static double mode_vrefresh(const Mode *m) { return m->fps; }
static int replies, modesets;
static void dau_reply_success(void*,void*) { ++replies; }
static void update(Context *ctx, Format *format, uint32_t previous_overlay_format=1,
                   uint32_t overlay_format=1, uint32_t target_w=3840,
                   uint32_t target_h=2160, double target_fps=60) {
    void *dau=nullptr, *reply=nullptr;
'''
tests = r'''
    ++modesets;
}
int main() {
    Context ctx; Format f;
    update(&ctx,&f); assert(replies==1 && modesets==0);
    sdi=false; update(&ctx,&f); assert(modesets==1); sdi=true;
    f.locked=false; update(&ctx,&f); assert(modesets==2); f.locked=true;
    ctx.modeset_active=false; update(&ctx,&f); assert(modesets==3); ctx.modeset_active=true;
    ctx.overlay_active=false; update(&ctx,&f); assert(modesets==4); ctx.overlay_active=true;
    update(&ctx,&f,2,1); assert(modesets==5);
    update(&ctx,&f,1,1,1920,2160); assert(modesets==6);
    update(&ctx,&f,1,1,3840,1080); assert(modesets==7);
    update(&ctx,&f,1,1,3840,2160,59.94); assert(modesets==8);
    update(&ctx,&f); assert(replies==2 && modesets==8);
}
'''
with tempfile.TemporaryDirectory(dir='/tmp/opencode', prefix='rx-idempotent-') as temp:
    p = Path(temp)
    (p / 'test.cpp').write_text(harness + branch + tests)
    subprocess.run(['c++', '-std=c++11', '-Wall', '-Wextra', '-Werror',
                    str(p / 'test.cpp'), '-o', str(p / 'test')], check=True)
    subprocess.run([str(p / 'test')], check=True)
print('PASS SDI DAU idempotence: unchanged running format only; HDMI/unlock/format changes still reconfigure')
