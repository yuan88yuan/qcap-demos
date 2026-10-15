#!/usr/bin/env python3
"""Host regression of the actual RX callback; no SDK/hardware emulation claim."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
app = (ROOT / 'src/tests/sc6f0-dante-rx.cpp').read_text()
start = app.index('\tstatic QRETURN on_vdec_event_callback(PVOID pUserData) {')
end = app.index('\n\tstatic void submit_vdec_packet(', start)
callback = app[start:end]
init = app[app.index('\tstatic void start_vdec_decoder_async('):
           app.index('\tstatic void stop_vdec_decoder(')]
stop = app[app.index('\tstatic void stop_vdec_decoder('):
           app.index('\tstatic void stop_drm_output(')]
assert 'if (output_is_sdi()) output_guard.lock();' in init
assert init.index('output_guard.lock()') < init.index('init_vdec_decoder(rx)')
assert init.index('output_guard.unlock()') < init.index('if (!ready) StopVdecAsync(rx)')
assert stop.index('qcap2_event_handlers_stop') < stop.index('decoder_guard')
assert stop.index('output_guard.lock()') < stop.index('qcap2_video_decoder_delete')

harness = r'''
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <future>
#include <mutex>
#include <thread>
using QRETURN = int;
using QRESULT = int;
using PVOID = void*;
constexpr int QCAP_RT_OK = 0, QCAP_RS_SUCCESSFUL = 0, QCAP_RS_ERROR_TIMEOUT = 1;
#define LOGE(...) ((void)0)
static bool sdi;
static bool output_is_sdi() { return sdi; }
struct qcap2_rcbuffer_t {};
struct zst_dante_rx_t {
    std::mutex decoder_lock, decoder_output_lock, display_lock;
    void* vdec = this;
    void* vdec_sink = this;
    bool vdec_started = true, display_enabled = true, vdec_sink_started = true;
    std::atomic<unsigned> decoded_frames{0}, decoded_errors{0}, decoder_display_errors{0};
};
static std::atomic<unsigned> pops{0}, pushes{0}, releases{0};
static qcap2_rcbuffer_t frame;
static int qcap2_video_decoder_pop(void*, qcap2_rcbuffer_t** out) {
    if (pops.fetch_add(1) == 0) { *out = &frame; return 0; }
    return QCAP_RS_ERROR_TIMEOUT;
}
static int qcap2_video_sink_push(void*, qcap2_rcbuffer_t* f) {
    assert(f == &frame); ++pushes; return 0;
}
static void qcap2_rcbuffer_release(qcap2_rcbuffer_t* f) {
    assert(f == &frame); ++releases;
}
'''
tests = r'''
int main() {
    for (bool backend : {true, false}) {
        sdi = backend;
        pops = pushes = releases = 0;
        zst_dante_rx_t rx;
        std::unique_lock<std::mutex> blocked_input(rx.decoder_lock);
        std::promise<void> entered;
        auto started = entered.get_future();
        auto worker = std::async(std::launch::async, [&] {
            entered.set_value(); return on_vdec_event_callback(&rx);
        });
        started.wait();
        bool independent_drain = worker.wait_for(std::chrono::milliseconds(250)) == std::future_status::ready;
        blocked_input.unlock(); // Always allow the old/HDMI callback to unwind.
        assert(worker.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
        assert(worker.get() == QCAP_RT_OK);
        assert(independent_drain == backend); // SDI fixed, HDMI unchanged.
        assert(rx.decoded_frames == 1 && rx.decoded_errors == 0);
        assert(pushes == 1 && releases == 1);
    }
    sdi = true;
    pops = 0;
    zst_dante_rx_t stopped;
    stopped.vdec_started = false;
    assert(on_vdec_event_callback(&stopped) == QCAP_RT_OK && pops == 0);

    zst_dante_rx_t rx;
    std::unique_lock<std::mutex> initializing(rx.decoder_output_lock);
    std::promise<void> entered;
    auto started = entered.get_future();
    auto worker = std::async(std::launch::async, [&] {
        entered.set_value(); return on_vdec_event_callback(&rx);
    });
    started.wait();
    bool gated = worker.wait_for(std::chrono::milliseconds(100)) == std::future_status::timeout;
    initializing.unlock();
    assert(worker.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
    worker.get();
    assert(gated);
    puts("PASS actual SDI RX callback: blocked input cannot stop output drain; output lifecycle gate; HDMI lock path unchanged");
}
'''
with tempfile.TemporaryDirectory(prefix='sdi-decoder-lock-', dir='/tmp/opencode') as tmp:
    src = Path(tmp) / 'test.cpp'
    binary = Path(tmp) / 'test'
    src.write_text(harness + callback + tests)
    subprocess.run(['c++', '-std=c++11', '-pthread', '-Wall', '-Wextra', '-Werror',
                    str(src), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True, timeout=10)
