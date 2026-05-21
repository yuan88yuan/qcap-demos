# Agent Guidance: qcap-demos

## Build System
- **Core**: GNU Make. Use root `<platform>.mk` files.
- **Requirement**: `QCAP_HOME` must point to the platform `qcap` library root.
- **Commands**: Use `./scripts/docker-run.sh qcap-dev:<platform> ./build.sh <platform> [target]` for all builds (including `utils`, `tests`, and `clean`).
- **Docker**: Builds run inside a Docker container via `./scripts/docker-run.sh`. The image tag convention is `qcap-dev:<platform>`. To build a new image, use `./scripts/docker-build.sh <base-image> qcap-dev:<platform>`.

## Project Structure
- `src/utils/`: Main sample application (`qdemo`).
- `src/tests/`: Individual test cases; each usually has a corresponding `.mk` file.
- `src/zzlab/`: Shared utility library (logging, clock, etc.).
- `mkfiles/`: Modular build logic (`rules.mk`, `funcs.mk`, `flags.mk`).
- `_objs/<platform>/`: All build artifacts (binaries in `bin/`, libs in `lib/`).

## Verification
- **Run Tests**: Binaries are located in `_objs/<platform>/bin/`. Execute them directly from there.
- **Build Order**: Ensure `QCAP_HOME` is correct before running `make`.

## Demo Creation
To create a new demo `<name>`:
1. Copy `src/tests/demo-template.cpp` to `src/tests/<name>.cpp`.
2. Copy `src/tests/demo-template.mk` to `src/tests/<name>.mk`.
3. Replace all occurrences of `demo-template` with `<name>` in the new files.
4. Replace all occurrences of `DEMO_TEMPLATE` with `<NAME>` (uppercase) in the new files.
5. Add `include $(S)/tests/<name>.mk` to `src/tests/tests.mk`.
