# Build-system migration plan: GNU Make -> CMake

## 1. Goal and acceptance criteria

Migrate `qcap-demos` from the current GNU Make implementation to CMake while preserving:

- the existing source-to-target mapping and target names;
- the current dependency graph, compile definitions, include paths, library order, and linker search/rpath-link behavior;
- the output layout (`_objs/<platform>/bin` and `_objs/<platform>/lib`);
- containerized builds and the platform-oriented command-line workflow;
- support for C++ and CUDA sources;
- the `utils`, `tests`, `all`, `clean`, `clean-lib`, `clean-bin`, `install`, and `package` workflows, with any currently no-op behavior documented.

The first migration gate is:

```text
image-tag = qcap-build:all3rdparty-ubuntu2004_x64-vaapi-20260720
platform  = ubuntu2004_x64
```

Do not remove the Make implementation until CMake passes the parity gate on this image.

## 2. Current system inventory

### Entry points and configuration

- `build.sh` must run inside the container and currently executes `make -f ${PLATFORM}.mk -j $(nproc)`.
- `ubuntu2004_x64.mk` selects `mkfiles/ubuntu2004_x64.mk`, sets `QCAP_HOME`, and enables `ZZLAB`, `QCAP`, `QCAP2_LIC`, `FONTCONFIG`, `FREETYPE`, and `HARFBUZZ`.
- `mkfiles/rules.mk` and `mkfiles/funcs.mk` generate compile, archive, link, dependency, clean, install, and package rules.
- `mkfiles/mods.mk` registers the module catalog and `src/utils/*.mk` / `src/tests/*.mk` register executable targets.
- `mkfiles/flags.mk` supplies platform/development feature definitions.
- The selected qcap build image supplies the platform-specific third-party headers and libraries; CMake should not expose a separate third-party path override.

### First-platform toolchain and paths

For `ubuntu2004_x64`, Make uses:

- `g++`, `gcc`, `ar`, `ranlib`, `strip`;
- `-std=c++11 -msse4` for C++, `-msse4` for C;
- `-Wl,-O1 -Wl,--hash-style=gnu -Wl,--as-needed` for linking;
- `/usr/local/cuda/bin/nvcc` for `.cu` files;
- `LINUX_GNU_LIB=lib/x86_64-linux-gnu`;
- `QCAP_HOME` for qcap headers/libs;
- `/opt/qcap/qcap-3rdparty/ubuntu2004_x64/{include,lib}` for third-party headers/libs.

The `BASE` module also supplies `-O3`, `-DBUILD_SYS=2`, `-DOS_LINUX`, Git commit/branch definitions, `-pthread`, `-lrt`, and `-ldl`.

### Targets to preserve

Utilities:

- `qdemo` (`src/utils/qdemo.cpp`)
- `sc6f0-dante-box` (`src/utils/sc6f0-dante-box.cpp`)
- `zdev-ticks-stat` (`src/utils/zdev-ticks-stat.cpp`)

Tests/demos:

- `test-avcap` (`.cpp` + `.cu`)
- `test-hsb-vsrc`, `test-deint`, `test-lic`, `bsci-demo`, `test-zznvenc2`, `test-ipx`, `kylin-demo`
- `sc6g0-aenc`, `test-aenc`, `test-freetype`, `test-graphics`
- `sc6f0-dante-demo`, `sc6f0-dante-tx`, `sc6f0-dante-rx`, `coe-demo`
- `test-rcbuf`, `roce-dmabuf-test`, `test-demuxer`, `sc6f0-sdi`
- `test-vdec-bench`, `test-swdec-bench`, `sc6n0-demuxer`

`test-swdec-bench` additionally links precompiled Jetson multimedia API objects. This must be treated as a platform-conditional input rather than accidentally compiled as a normal project source.

### Dependency/module model

Make modules are mostly imported dependency descriptions, not libraries built by this repository. The repository-built library is `zzlab` (`libzzlab.a`). qcap and qcap2 licensing are external libraries. The module catalog describes Boost, FFmpeg, FreeType, HarfBuzz, GStreamer, CUDA, CUDA driver/NPP, Fontconfig, zlib, iconv, x264, OpenSSL, SDL, ALSA, X11, VAAPI, udev, yuv, IPP/oneTBB, DRM, NVIDIA/Jetson libraries, SpeexDSP, fmt, ibverbs, IPX, Novatek HDAL, Allegro2, SIPL, and Dauservice.

CMake should model each module as either:

- a real target (`zzlab`); or
- an `INTERFACE IMPORTED` target carrying its include directories, compile definitions, link directories/options, and libraries.

This retains transitive dependency behavior without copying a large flat linker command into every executable.

## 3. Target CMake design

### Proposed files

Create:

```text
CMakeLists.txt
cmake/QcapPlatform.cmake       # platform/toolchain/feature mapping
cmake/QcapDependencies.cmake   # imported third-party/qcap targets
cmake/QcapTargets.cmake        # common target helpers and aggregates
cmake/platforms/ubuntu2004_x64.cmake
cmake/platforms/<other-platform>.cmake  # added in later porting phases
```

Keep source lists and target dependency declarations close to the source tree initially, or centralize them in `cmake/QcapTargets.cmake`; do not infer sources with broad globs. Explicit lists are required to match the current `tests.mk` and `utils.mk` registrations.

### Configuration variables/options

Use cache variables with names compatible with existing Make controls where practical:

- `QCAP_PLATFORM` (required; first value `ubuntu2004_x64`)
- `QCAP_HOME` (required or validated default)
- third-party dependencies are resolved from the selected build image's fixed platform layout;
- `SDKTARGETSYSROOT`, `DOCKER_HOME`, `NOVATEK_SDK`
- `BUILD_WITH_ZZLAB`, `BUILD_WITH_QCAP`, `BUILD_WITH_QCAP2_LIC`
- all `BUILD_WITH_*` module options, preserving current defaults;
- platform flags such as `BUILD_LINUX`, `BUILD_L4T`, `BUILD_UBUNTU2004_X64`, `BUILD_SC6N0`, and development flags such as `WITH_SSE`.

For the first platform, defaults must reproduce `ubuntu2004_x64.mk`: C++11, SSE4, Linux/Ubuntu definitions, and the enabled module set above. Validate required paths at configure time with clear errors, especially `QCAP_HOME`, qcap third-party include/lib directories, and CUDA when a CUDA source is enabled.

### Common target properties

Implement a helper such as `qcap_configure_target(target ...)` that applies:

- C++ standard and common warning/compatibility flags;
- `POSITION_INDEPENDENT_CODE` (Make applies `-fPIC` to C/C++ compilation);
- `BUILD_SYS=2`, `OS_LINUX`, Git metadata, and active platform/feature definitions;
- common include directory `/opt/qcap/qcap-3rdparty/${QCAP_PLATFORM}/include` and project include directory `${PROJECT_SOURCE_DIR}/include`;
- common link search locations and `-Wl,-rpath-link` equivalents;
- `Threads::Threads`, `rt`, and `dl` as appropriate;
- stripping policy only if parity requires it. Prefer preserving unstripped Debug builds and make stripping an explicit Release option, since unconditional Make stripping complicates debugging.

Use `target_compile_definitions` and `target_include_directories` rather than stringly-typed global flags. Use `target_link_libraries` for libraries and `target_link_options` for genuine linker options.

### Dependency targets and link semantics

Create imported/interface targets such as:

```text
qcap::qcap
qcap::qcap2_lic
qcap_demos::third_party
qcap_demos::boost
qcap_demos::ffmpeg
qcap_demos::freetype
qcap_demos::harfbuzz
qcap_demos::cuda
...
```

#### Treatment of the prebuilt qcap library

`QCAP_HOME` is an input SDK/library root, not a CMake subproject. CMake must never try to compile qcap sources. At configure time, validate and import the prebuilt artifacts explicitly:

```cmake
set(QCAP_HOME "" CACHE PATH "qcap installation/build root")
if(NOT QCAP_HOME)
  set(QCAP_HOME "${CMAKE_SOURCE_DIR}/../qcap-dev/qcap/build-qcap/_objs/${QCAP_PLATFORM}")
endif()

find_path(QCAP_INCLUDE_DIR NAMES qcap.h
  PATHS "${QCAP_HOME}/include" NO_DEFAULT_PATH REQUIRED)
find_library(QCAP_LIBRARY NAMES qcap
  PATHS "${QCAP_HOME}/lib" NO_DEFAULT_PATH REQUIRED)

add_library(qcap::qcap UNKNOWN IMPORTED)
set_target_properties(qcap::qcap PROPERTIES
  IMPORTED_LOCATION "${QCAP_LIBRARY}"
  INTERFACE_INCLUDE_DIRECTORIES "${QCAP_INCLUDE_DIR}"
  INTERFACE_COMPILE_DEFINITIONS "BUILD_WITH_QCAP=1")
```

Use the same pattern for `${QCAP_HOME}/lib/libqcap2_lic.so` (or its available static variant), with `BUILD_QCAP2=1` and `BUILD_WITH_QCAP2_LIC=1`. The implementation may use an interface target backed by validated `${QCAP_HOME}/lib` and link with `-L${QCAP_HOME}/lib -lqcap` rather than passing an absolute shared-library path. This preserves Make's `libqcap.so` NEEDED name while the qcap-only link directory prevents accidental host-library selection. Add a configure-time check that the selected library matches the target architecture and, when possible, exposes the expected ABI/SONAME.

The qcap target should also carry the equivalent link-directory/runtime-link metadata needed by consumers. The current Make build uses `${QCAP_HOME}/lib` as `-L` plus `-Wl,-rpath-link`, not an embedded runtime RPATH; preserve that distinction. If the deployed binary needs qcap discoverable at runtime, make that a deliberate install/package option rather than silently changing runtime search behavior.

Each executable that currently calls `add_mods(...,QCAP ...)` links `qcap::qcap` transitively through its target dependency list. `test-lic` additionally links `qcap::qcap2_lic`. `zzlab` remains the only repository-built library. Attach Make-equivalent definitions (`BUILD_WITH_QCAP`, `BUILD_QCAP2`, and the relevant `BUILD_WITH_*` definitions) to the dependency targets. For static and shared third-party libraries, use explicit imported locations or `find_library`; avoid relying on the host's unrelated `/usr/lib` copy when the Make build intentionally searches the qcap third-party tree.

Preserve special cases:

- qcap/qcap2 libraries come from `${QCAP_HOME}/lib`;
- HarfBuzz also links FreeType's static library;
- ALSA is static on `novatek_arm64` and shared elsewhere;
- FFmpeg adds `vdpau` on L4T;
- Jetson/NVIDIA modules use `SDKTARGETSYSROOT` and `LINUX_GNU_LIB`-dependent directories;
- `test-swdec-bench` has precompiled NVDEC objects;
- oneTBB's current install hook copies shared objects and needs an explicit CMake install/package decision.

### CUDA handling

The existing Make rule compiles every `.cu` source with `nvcc`, regardless of whether the CUDA dependency module is enabled. Reproduce this intentionally for `test-avcap` by enabling the CUDA language when that target is present, setting the CUDA compiler/toolkit from the container, and translating `GENCODE_FLAGS` to `CMAKE_CUDA_ARCHITECTURES` or a narrowly scoped `CUDA_NVCC_FLAGS` compatibility path. Do not silently drop `tests/test-avcap.cu` when `BUILD_WITH_CUDA=OFF`.

### Aggregate and output targets

Define:

- `utils`: dependency target for the three utility executables;
- `tests`: dependency target for all registered tests;
- `all`: the default build, at least depending on `utils` and preserving the existing Make meaning;
- individual executable targets with unchanged names;
- `zzlab` as the repository-built static library;
- `install` and `package` behavior explicitly, initially matching current no-op behavior except for any deliberately retained oneTBB copy/install operation.

Set output directories to `_objs/${QCAP_PLATFORM}/bin` and `_objs/${QCAP_PLATFORM}/lib`. Keep CMake's generated files in the platform build directory, but do not put generated objects into source directories.

## 4. Migration phases

### Phase 0 — Baseline and specification

1. In the specified image, clean and build the current Make system using the existing wrapper.
2. Record the exact target list, compiler/link commands (`VERBOSE=ON AT=`), output files, `file`/`readelf` architecture and dynamic dependencies, and the resolved qcap/third-party paths.
3. Capture the result of `make -pn -f ubuntu2004_x64.mk` for effective variables and dependency relationships.
4. Decide whether `install`/`package` remain no-op compatibility targets or receive a real install layout. Record the decision before writing CMake install rules.

Suggested baseline commands:

```bash
IMAGE=qcap-build:all3rdparty-ubuntu2004_x64-vaapi-20260720
./scripts/docker-run.sh "$IMAGE" ./build.sh ubuntu2004_x64 clean
./scripts/docker-run.sh "$IMAGE" ./build.sh ubuntu2004_x64 all tests VERBOSE=ON AT=
./scripts/docker-run.sh "$IMAGE" bash -lc \
  'find _objs/ubuntu2004_x64/bin -maxdepth 1 -type f -printf "%f\\n" | sort'
```

If the wrapper does not support the nested command exactly as shown, run a shell inside the image and execute the commands there; the build must still use the image and platform specified above.

### Phase 1 — CMake skeleton and Ubuntu configuration

1. Add the root project and platform/dependency helper files.
2. Add `zzlab` with the nine current source files and its public include directory.
3. Add qcap/qcap2 and the common external dependency target definitions.
4. Add all utility/test targets and aggregate targets using explicit source lists.
5. Add a temporary CMake build wrapper while retaining `build.sh` for Make.
6. Configure and build only `ubuntu2004_x64` in the specified image.

Deliverable: CMake can configure and produce the same executables and `libzzlab.a` in the same output directories without editing source files.

### Phase 2 — Compiler/link parity

For every executable, compare Make and CMake:

- source files and language used;
- preprocessor definitions;
- include directories and their order where it affects headers;
- C++/C/CUDA flags;
- static/shared library selection and order;
- `-L`, `rpath-link`, `pthread`, `rt`, and `dl` behavior;
- architecture, SONAME/NEEDED entries, and stripped status.

Resolve differences by improving target properties, not by adding a global opaque flag blob. Generate `compile_commands.json` for review.

### Phase 3 — Build wrapper and developer workflow

Replace or extend `build.sh` with a CMake implementation that preserves the container guard and accepts:

```text
./build.sh <platform> [target ...]
```

Recommended behavior:

- configure with `cmake -S . -B _objs/<platform>` when needed;
- build with `cmake --build _objs/<platform> --parallel $(nproc) --target <targets>`;
- map `clean`, `clean-lib`, and `clean-bin` to safe CMake/custom targets;
- pass `QCAP_HOME` and other paths from environment unless explicitly overridden;
- provide a separate `reconfigure`/`distclean` operation rather than deleting a build tree unexpectedly.

During transition, support `BUILD_SYSTEM=make|cmake` or retain a separate `build-make.sh`, so rollback is one command.

### Phase 4 — CI and regression gate

1. Add a CI job using the exact image/platform pair above.
2. Build both systems during the transition and compare artifact manifests.
3. Add a configure-only job to catch missing paths/options.
4. Add smoke tests for binaries that can run without capture hardware; mark hardware-dependent tests separately.
5. Update README, `ENV.md`, and `AGENTS.md` with the CMake commands and variable names.
6. Make CMake the default only after at least two successful CI runs and a maintainer review of linker/runtime parity.

### Phase 5 — Port remaining platforms

Port one platform family at a time: Ubuntu/Debian/CentOS x86, generic ARM64, L4T/Jetson, HiSilicon, Rockchip, Kylin, and Novatek. Convert each root `<platform>.mk` into a CMake toolchain/platform preset, preserving compiler, sysroot, `LINUX_GNU_LIB`, CUDA architecture, and platform definitions. Require a native or container build for each family before removing its Make configuration.

## 5. Verification matrix for the first gate

| Check | Expected result |
|---|---|
| Configure | Succeeds in the specified container with `QCAP_HOME` set |
| Default build | `all` builds utilities and `libzzlab.a` |
| Tests build | All targets listed by `tests.mk` are available; CUDA source is compiled |
| Artifact paths | Binaries under `_objs/ubuntu2004_x64/bin`, library under `lib` |
| Definitions | Includes `BUILD_SYS=2`, `OS_LINUX`, Git metadata, platform flags, and enabled module definitions |
| Linkage | qcap from `QCAP_HOME`; third-party libraries from the platform tree; `rt`, `dl`, and pthread behavior preserved |
| Incremental build | Touching one source rebuilds only the affected target/object |
| Clean | No generated files remain after the documented clean/distclean command |
| Runtime | `ldd`/`readelf -d` shows the intended NEEDED libraries and no accidental host dependency |
| Smoke tests | Non-hardware binaries execute or print help; hardware-dependent tests are explicitly reported rather than treated as build failures |

Compare artifact manifests with a script that records filename, ELF class/machine, size, `readelf -d`, and (for debug/non-stripped builds) symbols. Exact byte identity is not required; functional, ABI, and link-input parity is.

## 6. Risks and mitigations

- **External SDK layout varies by platform.** Centralize path resolution and fail early with the missing path and platform name.
- **CMake's transitive link behavior differs from Make's static/dynamic toggles.** Use imported targets and inspect verbose link lines for every dependency-heavy target.
- **CUDA compiler detection can fail in a host-only CMake configure.** Configure inside the build image and make CUDA language activation explicit and conditional on CUDA-source targets.
- **Current Make behavior contains implicit quirks** (for example, unconditional `.cu` compilation and no-op install/package). Baseline these behaviors before “cleaning them up.”
- **Cross-compilation is broader than the first Ubuntu target.** Keep platform configuration separate from target definitions and do not claim full migration until each platform family has a tested toolchain file.
- **The current CI file contains placeholder jobs.** Treat CI integration as a deliverable, not evidence that the old build is already tested.

## 7. Definition of done

The migration is complete when CMake is the default and documented build, the specified image/platform gate passes, all registered utilities/tests are represented, the wrapper and CI workflows work, external dependency/link behavior has been reviewed, and each supported platform has either been ported and verified or is explicitly listed as unsupported with a tracked follow-up. The Make files may then be archived or removed in a separate cleanup change.
