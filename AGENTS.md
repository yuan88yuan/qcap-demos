# Agent Guidance: qcap-demos

## Build System
- Builds must run inside the qcap Docker image.
- The first CMake migration target is:
  - image: `qcap-build:all3rdparty-ubuntu2004_x64-vaapi-20260720`
  - platform: `ubuntu2004_x64`
- `QCAP_HOME` must point to the prebuilt platform qcap library root. qcap is imported by CMake and is never built by this repository.
- The selected Docker image already contains the platform third-party headers and libraries under `/opt/qcap/qcap-3rdparty/<platform>`. Do not add or pass a `QCAP_3RDPARTY` override.
- Use CMake for the migration build:
  ```bash
  ./scripts/docker-run.sh qcap-build:all3rdparty-ubuntu2004_x64-vaapi-20260720 \
    env BUILD_SYSTEM=cmake ./build.sh ubuntu2004_x64 [target ...]
  ```
- CMake targets include `utils`, `tests`, individual utilities/tests, `clean`, `clean-bin`, `clean-lib`, `install`, `package`, and `distclean`.
- GNU Make remains the legacy compatibility build during migration:
  ```bash
  ./scripts/docker-run.sh <image-tag> ./build.sh <platform> [target]
  ```

## Project Structure
- `src/utils/`: Main sample applications, including `qdemo`.
- `src/tests/`: Individual test cases; each usually has a corresponding `.mk` file.
- `src/zzlab/`: Shared utility library (logging, clock, etc.).
- `mkfiles/`: Legacy GNU Make build logic (`rules.mk`, `funcs.mk`, `flags.mk`).
- `cmake/`: CMake platform, dependency, and target definitions.
- `CMakeLists.txt`: CMake entry point and build options.
- `_objs/<platform>/`: All build artifacts (binaries in `bin/`, libraries in `lib/`).

## Verification
- **Build artifacts**: Binaries are located in `_objs/<platform>/bin/`; libraries are in `_objs/<platform>/lib/`.
- **qcap linkage**: Verify that qcap resolves from `QCAP_HOME`, and inspect `readelf -d`/`ldd` when checking runtime dependencies.
- **Incremental cleanup**: Use `clean` for CMake's generated-file cleanup, `clean-bin`/`clean-lib` for output-only cleanup, and `distclean` to remove the complete platform CMake build tree.
- **Run tests**: Execute suitable binaries directly from `_objs/<platform>/bin/`. Hardware-dependent tests may require the target device.
- `test-swdec-bench` is excluded from the CMake `tests` aggregate when the external Jetson Multimedia API source tree is unavailable.
- `test-avcap` contains a CUDA source and requires the CUDA compiler in the selected image.
- **Build order**: Ensure the Docker image and `QCAP_HOME` are correct before configuring CMake or running the legacy Make build.

## Demo Creation
To create a new demo `<name>`:
1. Copy `src/tests/demo-template.cpp` to `src/tests/<name>.cpp`.
2. Copy `src/tests/demo-template.mk` to `src/tests/<name>.mk`.
3. Replace all occurrences of `demo-template` with `<name>` in the new files.
4. Replace all occurrences of `DEMO_TEMPLATE` with `<NAME>` (uppercase) in the new files.
5. Add `include $(S)/tests/<name>.mk` to `src/tests/tests.mk`.
6. Add the corresponding explicit source/dependency entry to `cmake/QcapTargets.cmake`; do not use broad source globs.
