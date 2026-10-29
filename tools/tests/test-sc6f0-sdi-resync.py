#!/usr/bin/env python3
"""Host-only watchdog policy regression; not DRM or media acceptance."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='sdi-resync-', dir='/tmp/opencode') as tmp:
    p = Path(tmp)
    (p / 'test.cpp').write_text(r'''
#include "sc6f0-sdi-resync-policy.h"
#include <cassert>
using sc6f0_sdi_resync::Policy;
int main() {
    assert(sc6f0_sdi_resync::known_fpga("0x26102101 SDIR 0x00000100"));
    assert(sc6f0_sdi_resync::known_fpga("0x26102201 SDIR 0x00000100"));
    assert(sc6f0_sdi_resync::known_fpga("0x26102701 SDIR 0x00000100"));
    assert(!sc6f0_sdi_resync::known_fpga("0x26102201 DNTX"));
    assert(!sc6f0_sdi_resync::known_fpga("0x26102202 SDIR"));
    assert(!sc6f0_sdi_resync::known_fpga(nullptr));
    assert(sc6f0_sdi_resync::has_suspect_alignment_history(0x000c09a7));
    assert(!sc6f0_sdi_resync::has_suspect_alignment_history(0x000c0186));
    assert(!sc6f0_sdi_resync::has_suspect_alignment_history(0x039c09a7));
    assert(!sc6f0_sdi_resync::has_suspect_alignment_history(UINT32_MAX));
    Policy p;
    for (unsigned t = 0; t < 10000; t += 500) {
        assert(!p.observe(t, 0x000c01a7, true)); // healthy ON
    }
    for (unsigned t = 10000; t < 20000; t += 500)
        assert(!p.observe(t, 0x039c09a7, true)); // observed OFF, history set
    for (unsigned t = 20000; t < 22000; t += 500)
        assert(!p.observe(t, 0x000c09a7, true));
    assert(p.observe(22000, 0x000c09a7, true));
    for (unsigned t = 22500; t < 29000; t += 500)
        assert(!p.observe(t, 0x000c09a7, true)); // cooldown + debounce
    assert(p.observe(29000, 0x000c09a7, true));
    for (unsigned t = 29500; t < 36000; t += 500)
        assert(!p.observe(t, 0x000c09a7, true));
    assert(p.observe(36000, 0x000c09a7, true));
    for (unsigned t = 36500; t < 82000; t += 500)
        assert(!p.observe(t, 0x000c09a7, true)); // max3/min, sticky won't storm
    assert(p.observe(82000, 0x000c09a7, true)); // rolling window expires
    p.failed = true;
    for (unsigned t = 82500; t < 180000; t += 500)
        assert(!p.observe(t, 0x000c09a7, true));
    Policy q;
    assert(!q.observe(0, 0x000c09a7, true));
    assert(!q.observe(500, 0x000c09a7, true));
    assert(!q.observe(1000, 0x000c0186, true)); // transient cleared
    assert(!q.observe(1500, 0x000c09a7, false)); // wrong route
    assert(!q.observe(2000, UINT32_MAX, true)); // invalid read
    assert(!q.observe(2500, 0x000c09a7, true));
    assert(!q.observe(10000, 0x000c09a7, true)); // long dispatch gap resets debounce
    assert(!q.observe(10500, 0x000c09a7, true));
    assert(!q.observe(11000, 0x000c09a7, true));
    assert(!q.observe(11500, 0x000c09a7, true));
    assert(q.observe(12000, 0x000c09a7, true));
}
''')
    subprocess.run(['c++', '-std=c++11', '-Wall', '-Wextra', '-Werror',
                    '-I', str(ROOT / 'src/tests'), str(p / 'test.cpp'),
                    '-o', str(p / 'test')], check=True)
    subprocess.run([str(p / 'test')], check=True)
print('PASS: debounce, ON/OFF samples, eligibility, cooldown, rolling limit, failure latch, sampling gaps')

app = (ROOT / 'src/tests/sc6f0-dante-rx.cpp').read_text()
start = app.index('\tstatic bool reinitialize_sdi_output(')
end = app.index('\n\t// Transitional workaround callback:', start)
with tempfile.TemporaryDirectory(prefix='sdi-resync-drm-', dir='/tmp/opencode') as tmp:
    p = Path(tmp)
    harness = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>
#define LOGE(...) ((void)0)
enum { DRM_MODE_OBJECT_CRTC=1, DRM_MODE_OBJECT_CONNECTOR=2, DRM_MODE_OBJECT_PLANE=3,
       DRM_MODE_ATOMIC_ALLOW_MODESET=0x400, DRM_MODE_ATOMIC_TEST_ONLY=0x100 };
struct drmModeModeInfo { uint32_t clock=594000, width=3840, height=2160; };
struct ultra_direct_context_t {
    int drm_fd=3; uint32_t crtc_id=10, connector_id=20, primary_plane_id=30,
        overlay_plane_id=40, primary_fb_id=53, overlay_fb_id=54;
    drmModeModeInfo current_mode;
};
struct drmModePropertyBlobRes { uint32_t length; void* data; };
struct drmModeAtomicReq { uint64_t active=2, mode=0; };
struct Call { uint32_t flags; uint64_t active, mode; };
static std::vector<Call> calls;
static unsigned fail_step=0;
static uint64_t live_frac=0;
static bool drm_property_value(int, uint32_t, uint32_t, const char* name, uint64_t* v) {
    if (!strcmp(name,"ACTIVE") || !strcmp(name,"SC6F0_OVERLAY_SOURCE")) *v=1;
    else if (!strcmp(name,"MODE_ID")) *v=42;
    else if (!strcmp(name,"is_frac")) *v=live_frac;
    return true;
}
// Distinguish live FBs by object, while keeping all other property stubs above.
static bool get_value(int fd,uint32_t obj,uint32_t kind,const char* name,uint64_t* v) {
    if (!strcmp(name,"FB_ID")) { *v=obj==30 ? 53 : 54; return true; }
    return drm_property_value(fd,obj,kind,name,v);
}
#define drm_property_value get_value
static drmModePropertyBlobRes* drmModeGetPropertyBlob(int, uint32_t id) {
    assert(id==42); return new drmModePropertyBlobRes{sizeof(drmModeModeInfo),new drmModeModeInfo};
}
static void drmModeFreePropertyBlob(drmModePropertyBlobRes* b) { delete static_cast<drmModeModeInfo*>(b->data); delete b; }
static bool is_sdi_mode(const drmModeModeInfo&) { return true; }
static int drmModeCreatePropertyBlob(int,void*,uint32_t,uint32_t* id) { *id=99; return 0; }
static void drmModeDestroyPropertyBlob(int,uint32_t id) { assert(id==99); }
static drmModeAtomicReq* drmModeAtomicAlloc() { return new drmModeAtomicReq; }
static void drmModeAtomicFree(drmModeAtomicReq* r) { delete r; }
static uint32_t drm_property_id(int,uint32_t,uint32_t,const char* name) {
    return !strcmp(name,"ACTIVE") ? 1 : !strcmp(name,"MODE_ID") ? 2 : 3;
}
static int drmModeAtomicAddProperty(drmModeAtomicReq* r,uint32_t obj,uint32_t prop,uint64_t val) {
    assert(obj==10 || obj==20); // Never change any FB/plane property.
    if (prop==1) r->active=val;
    else if (prop==2) r->mode=val;
    else { assert(prop==3 && obj==20 && val==0); }
    return 0;
}
static int drmModeAtomicCommit(int,drmModeAtomicReq* r,uint32_t flags,void*) {
    calls.push_back({flags,r->active,r->mode});
    return calls.size()==fail_step ? -1 : 0;
}
'''
    tests = r'''
int main() {
    ultra_direct_context_t ctx;
    assert(reinitialize_sdi_output(&ctx));
    assert(calls.size()==4);
    assert(calls[0].flags==0x500 && calls[0].active==1);
    assert(calls[1].flags==0x500 && calls[1].active==0);
    assert(calls[2].flags==0x400 && calls[2].active==0);
    assert(calls[3].flags==0x400 && calls[3].active==1 && calls[3].mode==99);
    for (unsigned step=1;step<=4;++step) {
        calls.clear(); fail_step=step;
        assert(!reinitialize_sdi_output(&ctx));
        assert(calls.size()==(step<=2 ? step : step+1));
        if (step>2) {
            assert(calls.back().active==1 && calls.back().mode==42);
            assert(calls.back().flags==0x400);
        }
    }
    calls.clear(); live_frac=1;
    assert(!reinitialize_sdi_output(&ctx) && calls.empty());
    live_frac=0; ctx.overlay_fb_id=55;
    assert(!reinitialize_sdi_output(&ctx) && calls.empty());
}
'''
    (p / 'test.cpp').write_text(harness + app[start:end] + tests)
    subprocess.run(['c++', '-std=c++11', '-Wall', '-Wextra', '-Werror',
                    str(p / 'test.cpp'), '-o', str(p / 'test')], check=True)
    subprocess.run([str(p / 'test')], check=True)
print('PASS: actual App DRM helper mocked; both TEST_ONLY before ACTIVE, planes retained, rollback, live-state guards')
