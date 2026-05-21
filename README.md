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

Choose a target platform and run the build script inside a Docker container. For example, to compile for the `xlnk2_arm64` platform:

```bash
# Replace xlnk2_arm64 with your target platform
./scripts/docker-run.sh qcap-dev:xlnk2_arm64 ./build.sh xlnk2_arm64
```

To also clean, add the `clean` target:

```bash
./scripts/docker-run.sh qcap-dev:xlnk2_arm64 ./build.sh xlnk2_arm64 clean
```

After a successful compilation, all generated files (including executables) will be placed in the `_objs/<your-target-platform>/` directory.

### 2. Execution

The executables are located in the corresponding platform's `_objs` subdirectory.

```bash
# Run the qdemo sample application
./_objs/<platform>/bin/qdemo
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
