# qcap-demos yuantest

This is a C++ project that demonstrates and tests the features of the `qcap` video and audio capture library.

This project includes a main sample application (`qdemo`), a utility library (`zzlab`), and a complex `make`-based build system that provides a high degree of cross-platform compatibility.

## Directory Structure

```
.
├── src/
│   ├── utils/         # The main sample application
│   ├── tests/         # Test applications
│   └── zzlab/         # Shared utility library (logging, clock, etc.)
├── mkfiles/           # Make configuration files for various platforms and libraries
├── _objs/             # Compiled object files and executables
└── ...
```

## Getting Started

This project uses `make` for compilation. You need to build for a target platform defined in the `mkfiles` directory.

### 1. Compilation

Choose a target platform and run the `make` command. For example, to compile for the `l4t-r36-2` (NVIDIA Jetson L4T R36.2) platform:

```bash
# Replace l4t-r36-2 with your target platform and QCAP_HOME with the path to the qcap library
QCAP_HOME=/your-qcap-root/l4t-r36-2 make -f l4t-r36-2.mk -j4
```

After a successful compilation, all generated files (including executables) will be placed in the `_objs/<your-target-platform>/` directory.

### 2. Execution

The executables are located in the corresponding platform's `_objs` subdirectory.

```bash
# Run the qdemo sample application
./_objs/l4t-r36-2/qdemo
```

## Main Technologies and Dependencies

This project integrates various video/audio processing and system-related libraries to run on different hardware and operating systems. The main technologies and dependencies include:

*   **Core**: C++
*   **Build System**: GNU Make
*   **Video/Audio Processing**:
    *   FFmpeg
    *   GStreamer
    *   NVIDIA CUDA / NPP / NvJpeg
    *   VAAPI
    *   X264
*   **Supported Platforms**:
    *   Linux (Ubuntu, CentOS, Debian, Kylin)
    *   Embedded Platforms (NVIDIA L4T, HiSilicon, Rockchip, Novatek)
*   **Other Libraries**:
    *   SDL
    *   ALSA
    *   OpenSSL
    *   Boost
    *   fmt

## Examples

### `example-rcbuffer-custom-payload`

This example shows how to use a **custom payload** container struct that embeds `qcap2_av_frame_t` together with user-owned metadata and image planes, then relies on a callback-based finalizer (`qcap2_rcbuffer_new(..., on_free_callback)`) to safely release payload resources.

Run it from a built target directory:

```bash
./_objs/<target>/example-rcbuffer-custom-payload
```
