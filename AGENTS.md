# Agent Guidance: qcap-demos

## Build System
- **Core**: Custom GNU Make system.
- **Entry Points**: Use root `.mk` files for target platforms (e.g., `l4t-r36-2.mk`).
- **Requirement**: `QCAP_HOME` environment variable must point to the platform-specific `qcap` library root.
- **Commands**:
  - Build all: `make -f <platform>.mk`
  - Build utilities: `make -f <platform>.mk utils`
  - Build tests: `make -f <platform>.mk tests`
  - Clean: `make -f <platform>.mk clean`
- **Docker Wrapper**: Use `./build.sh <platform> [target]` to run builds inside a container.
- **Docker Daemon**: Start a docker container using `../docker-scripts/docker-daemon.sh yuan88yuan/<platform>:v1 <platform>`
- **Build Process**: ALWAYS build project by `./build.sh`
- **Check**: Check if docker container is started before build project

## Project Structure
- `src/utils/`: Main sample application (`qdemo`).
- `src/tests/`: Individual test cases; each usually has a corresponding `.mk` file.
- `src/zzlab/`: Shared utility library (logging, clock, etc.).
- `mkfiles/`: Modular build logic (`rules.mk`, `funcs.mk`, `flags.mk`).
- `_objs/<platform>/`: All build artifacts (binaries in `bin/`, libs in `lib/`).

## Verification
- **Run Tests**: Binaries are located in `_objs/<platform>/bin/`. Execute them directly from there.
- **Build Order**: Ensure `QCAP_HOME` is correct before running `make`.
