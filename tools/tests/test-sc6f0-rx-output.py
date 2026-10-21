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
    std::array<unsigned char,256> e;
    for (uint32_t width : {1920u,3840u}) {
        const uint32_t height = width == 1920 ? 1080 : 2160;
        assert(sdi_capabilities(width,height,60,e));
        for (size_t base : {size_t(0),size_t(128)}) {
            unsigned int sum = 0;
            for (size_t i=0;i<128;++i) sum += e[base+i];
            assert((sum&255)==0);
        }
        assert(e[0]==0 && e[1]==255 && e[7]==0 && e[126]==1);
        assert(e[20]==0xa0 && e[24]==2); // RGB8, preferred DTD
        for(size_t i=35;i<38;++i) assert(e[i]==0); // no established timings
        for(size_t i=38;i<54;++i) assert(e[i]==1); // no standard timings
        assert((e[54]|(unsigned(e[55])<<8))*10 == (width==1920 ? 148500u : 594000u));
        assert((unsigned(e[56])|((e[58]>>4)<<8))==width);
        assert((unsigned(e[59])|((e[61]>>4)<<8))==height);
        const unsigned htotal = width + (e[57]|((e[58]&15)<<8));
        const unsigned vtotal = height + (e[60]|((e[61]&15)<<8));
        assert((e[54]|(unsigned(e[55])<<8))*10000u == htotal*vtotal*60u);
        for(size_t i : {size_t(72),size_t(90),size_t(108)}) {
            assert(e[i]==0 && e[i+1]==0); // no additional DTD
        }
        assert(e[128]==2 && e[129]==3 && e[130]==8);
        assert(e[131]==0); // no YCbCr444/422 or extra CTA DTDs
        assert(e[132]==0x23 && e[133]==0x09 && e[134]==4 && e[135]==4);
        // No video VIC (60-family CTA VICs also admit 59.94); sole exact DTD.
        for(size_t i=136;i<255;++i) assert(e[i]==0); // no VSDB/HDR/extra modes
    }
    assert(!sdi_capabilities(1920,1080,59,e));
    assert(!sdi_capabilities(3840,1080,60,e));
    assert(!sdi_capabilities(1280,720,60,e));
    for(auto byte : e) assert(byte==0); // unsupported mode never returns a usable EDID
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
