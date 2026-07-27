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

The project is migrating from GNU Make to CMake. CMake is available for the `ubuntu2004_x64` platform while Make remains the default compatibility path during migration. Builds must run inside the qcap build container.

### 1. Compilation

For the first CMake migration gate:

```bash
IMAGE=qcap-build:all3rdparty-ubuntu2004_x64-vaapi-20260720

# Default utilities build
./scripts/docker-run.sh "$IMAGE" env BUILD_SYSTEM=cmake ./build.sh ubuntu2004_x64

# Build the registered test aggregate
./scripts/docker-run.sh "$IMAGE" env BUILD_SYSTEM=cmake ./build.sh ubuntu2004_x64 tests
```

`QCAP_HOME` may be supplied from the environment or with a CMake cache override. It must contain the prebuilt qcap headers and libraries. The selected build image supplies the platform-specific third-party headers and libraries.

During the transition, the legacy Make build remains available:

```bash
./scripts/docker-run.sh "$IMAGE" ./build.sh ubuntu2004_x64
```

After a successful compilation, generated files (including executables) are placed in `_objs/<your-target-platform>/`.

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
