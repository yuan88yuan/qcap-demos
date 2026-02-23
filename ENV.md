# Environment Variables and Build Controls

This document lists the environment variables and Make variables used to control the build system in `qcap-demos`.

## 1. Core Build Controls

| Variable | Usage | Default Value |
| :--- | :--- | :--- |
| `PLATFORM` | **Required.** Specifies the target platform. Usually set by the root `.mk` file. | N/A |
| `VERBOSE` | Set to `ON` to print detailed internal build system debugging information. | (Empty) |
| `AT` | Controls command echoing. Set to empty (e.g., `make AT=`) to show all commands executed. | `@` (Silent) |
| `S` | Path to the source code directory. | `src` |
| `D` | Path to the build output directory. | `_objs/${PLATFORM}` |

## 2. Feature & Module Toggles

These variables enable or disable specific modules and features. They are typically set to `ON` or `OFF`.

*   `BUILD_WITH_ZZLAB`: Enable the `zzlab` utility module.
*   `BUILD_WITH_QCAP`: Enable the `qcap` capture library integration.
*   *Specific modules:* `BUILD_WITH_CUDA`, `BUILD_WITH_FFMPEG`, `BUILD_WITH_GSTREAMER`, `BUILD_WITH_ALSA`, `BUILD_WITH_SDL`, etc.
*   *Platform flags:* `BUILD_LINUX`, `BUILD_L4T`, `BUILD_HISIV`, `BUILD_DESKTOP`.

## 3. Path & Dependency Configuration

These variables are critical for cross-compilation and dependency management.

| Variable | Usage |
| :--- | :--- |
| `SDKTARGETSYSROOT` | The path to the target system's root directory (sysroot) for cross-compiling. |
| `DOCKER_HOME` | The base path for project-internal dependencies, often used in containerized builds. |
| `QCAP_3RDPARTY` | **External Dependency Path.** Points to the directory containing pre-built third-party libraries (like FFmpeg, SDL, ALSA, etc.) for the specific target platform. It is used in `base.mk` to populate global include and library search paths. |
| `QCAP_HOME` | **Core Library Path.** Points to the root directory of the `qcap` library (either its source or its build output). This is used by modules like `qcap.mk` and `qcap2-lic.mk` to find the core capture headers and link against the `libqcap.so`/`libqcap.a` files. |
| `NOVATEK_SDK` | Path to the Novatek hardware SDK. |
| `LINUX_GNU_LIB` | The architecture-specific library subdirectory name (e.g., `lib/aarch64-linux-gnu`). |

## 4. Toolchain Configuration

These variables define the compilers and binary tools used during the build.

| Variable | Usage |
| :--- | :--- |
| `CROSS_COMPILE` | Prefix for the cross-compiler toolchain (e.g., `aarch64-linux-gnu-`). |
| `PATH_EXT` | A path that is prepended to the system `PATH` to find specific toolchain binaries. |
| `CXX` | The C++ compiler command. |
| `CC` | The C compiler command. |
| `NVCC` | The NVIDIA CUDA compiler command. |
| `STRIP` | The command used to strip debug symbols from binaries. |
| `AR` | The archiver command used to create static libraries. |
| `RANLIB` | The command used to generate index to archive. |

## 5. Build Flags

Standard compiler and linker flags that can be overridden or extended.

*   `CXXFLAGS`: Flags for the C++ compiler.
*   `CFLAGS`: Flags for the C compiler.
*   `LDFLAGS`: Flags for the linker.
*   `CUFLAGS`: Flags for the CUDA compiler.

## 6. Metadata

These are usually auto-detected but can be overridden if necessary.

*   `GIT_COMMIT_HASH`: The short hash of the current git commit.
*   `GIT_BRANCH_NAME`: The name of the current git branch.
