# Agent Guidance: qcap-demos

## Build System
- **Core**: GNU Make. Use root `<platform>.mk` files.
- **Requirement**: `QCAP_HOME` must point to the platform `qcap` library root.
- **Commands**: Use `./build.sh <platform> [target]` for all builds (including `utils`, `tests`, and `clean`).
- **Docker**: Ensure container is running via `../docker-scripts/docker-daemon.sh yuan88yuan/<platform>:v1 <platform>` before building.

## Project Structure
- `src/utils/`: Main sample application (`qdemo`).
- `src/tests/`: Individual test cases; each usually has a corresponding `.mk` file.
- `src/zzlab/`: Shared utility library (logging, clock, etc.).
- `mkfiles/`: Modular build logic (`rules.mk`, `funcs.mk`, `flags.mk`).
- `_objs/<platform>/`: All build artifacts (binaries in `bin/`, libs in `lib/`).

## Verification
- **Run Tests**: Binaries are located in `_objs/<platform>/bin/`. Execute them directly from there.
- **Build Order**: Ensure `QCAP_HOME` is correct before running `make`.
