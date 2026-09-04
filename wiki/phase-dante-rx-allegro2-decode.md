# Phase: Dante RX — qcap2 ALLEGRO2 Hardware Decode

## Overview

Replace the stats-only `rx_video_sink` in `sc6f0-dante-rx.cpp` with a qcap2 video decoder using `QCAP_ENCODER_TYPE_ALLEGRO2` for hardware-accelerated H.264 decoding on sc6f0.

**Source file:** `src/tests/sc6f0-dante-rx.cpp`
**Build file:** `src/tests/sc6f0-dante-rx.mk` (no changes needed — already links QCAP ALLEGRO2)

## Data Flow

```
Dante coordinator RX output (H.264 Annex-B bitstream)
    │
    ▼
rx_video_sink_push()
    ├── existing: stats (packets, bytes, NAL types, keyframes)
    ├── extract SPS/PPS from first keyframe
    ├── parse SPS → width, height, framerate
    ├── init qcap2_video_decoder (ALLEGRO2, H264, NV12)
    └── qcap2_video_decoder_push() per packet
            │
            ▼ (event callback)
        qcap2_video_decoder_pop() → decoded_frames++
```

## Implementation Steps

### Step 1: Add qcap2 includes + struct fields
- Add `#include <qcap2.h>` and `#include <qcap.common.h>`
- Add decoder fields to `zst_dante_rx_t`: `vdec`, `vdec_event`, `vdec_event_handlers`, `vdec_started`, `extra_data`, `extra_data_size`, `dec_width`, `dec_height`, `dec_fps`, `decoded_frames`, `decoded_errors`
- **Compile test:** build passes, no behavior change

### Step 2: Add H.264 bitstream utilities
- Implement `h264_bitstream_t` struct with `bs_read_bit`, `bs_read_bits`, `bs_read_ue`, `bs_read_se`
- Implement `h264_parse_sps()` — parse SPS NAL for width/height/framerate
- Implement `h264_extract_sps_pps()` — scan Annex-B start codes, extract SPS (type 7) and PPS (type 8) NAL units
- **Compile test:** utilities compile, not yet called

### Step 3: Add decoder init + event callback
- Implement `on_video_decoded()` — event callback that calls `qcap2_video_decoder_pop()` in a loop, increments `decoded_frames`
- Implement `init_vdec_decoder()` — creates decoder, configures ALLEGRO2 property, sets extra_data (SPS+PPS), creates event+handlers, starts decoder
- **Compile test:** functions compile, not yet called from push

### Step 4: Modify `rx_video_sink_push`
- On first keyframe (before decoder started): call `h264_extract_sps_pps()` → `h264_parse_sps()` → `init_vdec_decoder()`
- For each packet when decoder started: `malloc` copy → `qcap2_rcbuffer_new(copy, free)` → `qcap2_video_decoder_push()` → `qcap2_rcbuffer_delete()`
- **Compile test:** full decode path compiles

### Step 5: Enhance stats + cleanup
- Add decoded frame stats to periodic log in `dante_rx_bus_worker`
- Add decoded frame summary to final report in `stop_dante_rx`
- Add decoder cleanup (stop/delete decoder, event handlers, event, free extra_data) in `stop_dante_rx`
- **Compile test:** full build passes

## qcap2_rcbuffer_t Lifecycle

```
malloc(copy) ← H.264 data from zst_buffer_t
    │
    ▼
qcap2_rcbuffer_new(copy, free)  ← wraps caller-owned buffer
    │
    ▼
qcap2_video_decoder_push(vdec, rcbuf)  ← decoder copies into AL stream buffer
    │
    ▼
qcap2_rcbuffer_delete(rcbuf)  ← calls free(copy)
```

## SPS Parser Reference

Minimal H.264 SPS fields needed for resolution:
- `profile_idc` → determines optional fields (chroma_format_idc, etc.)
- `pic_width_in_mbs_minus1` → width = (val + 1) * 16
- `pic_height_in_map_units_minus1` → height = (2 - frame_mbs_only_flag) * (val + 1) * 16
- `frame_cropping_flag` → subtract crop values
- VUI `timing_info` → fps = time_scale / (2 * num_units_in_tick)

Reference implementation: `qcap-dev/qcap/src/ireader/h264/source/h264-sps.c`

## Build Verification

```bash
# Inside qcap Docker container:
cd /workspace
./scripts/docker-run.sh <image-tag> ./build.sh ubuntu2004_x64 sc6f0-dante-rx
```

## Testing

1. Start TX: `sc6f0-dante-tx` on source device (HDMI input → Allegro2 encode → Dante)
2. Start RX: `sc6f0-dante-rx` on receiving device
3. Route via Dante Controller
4. Verify in RX logs:
   - `qcap2 ALLEGRO2 decoder started: WxH @ fps fps`
   - Decoded frame count incrementing
   - No decode errors
