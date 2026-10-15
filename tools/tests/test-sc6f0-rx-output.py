#!/usr/bin/env python3
"""Host C++ policy tests only: no ALSA/DRM/DAU or named-board acceptance."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
app = (ROOT / 'src/tests/sc6f0-dante-rx.cpp').read_text()
start = app.index('\tstatic bool is_output_connector(')
end = app.index('\n\tstruct zst_dante_rx_t;', start)
callbacks = app[start:end]
with tempfile.TemporaryDirectory(prefix='sc6f0-rx-output-', dir='/tmp/opencode') as tmp:
    p = Path(tmp)
    source = p / 'test.cpp'
    harness = r'''
#include "sc6f0-dante-rx-output.h"
#include <cassert>
#include <limits>
#include <cstdio>
using namespace sc6f0_rx_output;
static Backend backend = Backend::HDMI;
static bool output_is_sdi() { return backend == Backend::SDI; }
#define DRM_MODE_CONNECTOR_HDMIA 11
#define DRM_MODE_CONNECTOR_Unknown 0
#define DRM_MODE_OBJECT_CONNECTOR 1
#define DRM_MODE_FLAG_INTERLACE 1
#define DRM_MODE_FLAG_DBLSCAN 2
#define LOGE(...) ((void)0)
struct drmModeConnector { uint32_t connector_type, connector_id; };
struct drmModeModeInfo { uint32_t hdisplay, vdisplay, clock, htotal, vtotal, flags; };
struct drmModeAtomicReq { int count = 0; };
static std::string missing;
static uint32_t drm_property_id(int fd, uint32_t obj, uint32_t type, const char* name)
{ (void)fd; (void)obj; (void)type; return missing == name ? 0 : 1; }
static bool atomic_error;
static int drmModeAtomicAddProperty(drmModeAtomicReq* req, uint32_t obj, uint32_t id, uint64_t value)
{ (void)obj; (void)id; (void)value; if (atomic_error) return -1; return ++req->count; }
'''
    tests = r'''
int main() {
    assert(from_product("SC6F0 SDI Dante RX") == Backend::SDI);
    assert(from_product("SC6F0 SDI Dante RX V1") == Backend::SDI);
    assert(from_product("SC6F0 Dante RX") == Backend::HDMI);
    assert(from_product("SC6F0 Dante TX") == Backend::HDMI);
    assert(from_product("SC6F0 SDI Dante TX") == Backend::HDMI);
    assert(from_product("") == Backend::HDMI);
    assert(sdi_mode(1920,1080,60,148500,false));
    assert(sdi_mode(3840,2160,60,594000,false));
    assert(!sdi_mode(3840,2160,60,533250,false));
    assert(!sdi_mode(1920,1080,59.94,148500,false));
    assert(!sdi_mode(1920,1080,60,148500,true));
    assert(!sdi_mode(1280,720,60,74250,false));
    assert(!sdi_mode(3840,2160,std::numeric_limits<double>::quiet_NaN(),594000,false));
    auto hd = sdi_properties(1920,1080), uhd = sdi_properties(3840,2160);
    assert(hd[0].value == 2 && hd[1].value == 2);
    assert(uhd[0].value == 5 && uhd[1].value == 8);
    assert(hd[2].value == 0 && hd[6].value == 1920 && hd[7].value == 1080);
    assert(uhd[8].value == MEDIA_BUS_FMT_RGB888_1X24);
    assert(uhd[9].value == MEDIA_BUS_FMT_UYVY10_1X20 && uhd[10].value == 0);
    assert(sdi_card_id("xlnxsdi0") && sdi_card_id("xlnxsdi1"));
    assert(sdi_card_id("xlnx-sdi12") && sdi_card_id("xlnx_sdi"));
    assert(!sdi_card_id("xlnxhdmi1") && !sdi_card_id("xlnxi2s0"));
    assert(!sdi_card_id("xlnxsdiOther") && !sdi_card_id(""));
    auto e = sdi_capabilities();
    for (size_t base : {size_t(0),size_t(128)}) {
        unsigned int sum = 0;
        for (size_t i=0;i<128;++i) sum += e[base+i];
        assert((sum&255)==0);
    }
    assert(e[0]==0 && e[1]==255 && e[7]==0 && e[126]==1);
    assert(e[20]==0xa0); // digital RGB8, unspecified physical interface
    assert((e[54]|(unsigned(e[55])<<8))*10 == 148500);
    assert((e[72]|(unsigned(e[73])<<8))*10 == 594000);
    assert(e[128]==2 && e[129]==3 && e[130]==11);
    assert(e[132]==0x42 && e[133]==16 && e[134]==97);
    assert(e[135]==0x23 && e[136]==0x09 && e[137]==4 && e[138]==4);
    for(size_t i=139;i<255;++i) assert(e[i]==0); // no VSDB/HDR/extra modes
    drmModeConnector hdmi = {DRM_MODE_CONNECTOR_HDMIA, 10};
    drmModeConnector sdi = {DRM_MODE_CONNECTOR_Unknown, 11};
    drmModeModeInfo mode = {1920,1080,148500,2200,1125,0};
    drmModeAtomicReq req;
    assert(is_output_connector(0,&hdmi) && !is_output_connector(0,&sdi));
    assert(add_output_properties(0,10,&req,mode) && req.count == 0); // HDMI untouched
    backend = Backend::SDI;
    assert(!is_output_connector(0,&hdmi) && is_output_connector(0,&sdi));
    missing = "sdi_mode";
    assert(!is_output_connector(0,&sdi));
    missing.clear();
    assert(is_sdi_mode(mode));
    assert(add_output_properties(0,11,&req,mode) && req.count == 11);
    missing = "height_out";
    assert(!add_output_properties(0,11,&req,mode));
    missing.clear(); atomic_error = true;
    assert(!add_output_properties(0,11,&req,mode));
    atomic_error = false; mode.clock = 148352;
    assert(!is_sdi_mode(mode) && !add_output_properties(0,11,&req,mode));
    puts("PASS SC6F0 RX output policy: backend isolation, integer SDI modes/properties, ALSA IDs, conservative synthetic capability checksums");
}
'''
    source.write_text(harness + callbacks + tests)
    binary = p / 'test'
    subprocess.run(['c++', '-std=c++11', '-Wall', '-Wextra', '-Werror', '-I',
                    str(ROOT / 'src/tests'), str(source), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
