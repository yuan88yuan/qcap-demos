# SC6F0 Dante RX/TX clean-clone build guide

This guide describes the external inputs and repeatable build order for
`sc6f0-dante-rx` and `sc6f0-dante-tx` after a fresh `qcap-demos` clone. The
build is cross-compilation evidence for SC6F0; it does not replace testing on
the target board.

## Fixed build contract

- Target platform: `xlnk2_arm64`.
- Builder image: `qcap-build:xlnk2_arm64-base`.
- Use the CMake migration build for these apps, with `BUILD_SC6F0=ON`.
- Keep the sibling repositories in the workspace layout below. CMake defaults
  `QCAP_HOME` to the sibling QCAP build and `ZSTREAMER_ROOT` to the sibling
  zstreamer tree.
- The image supplies the cross SDK and platform payload under
  `/opt/qcap/qcap-3rdparty/xlnk2_arm64`. Do not pass a `QCAP_3RDPARTY` override.
- `qcap-demos/3rdparty/` is gitignored. A fresh clone does **not** contain the
  DAU Service header or library installed there by a previous build.

Expected layout (the sibling names are significant for the defaults):

```text
~/docker/
  qcap-dev/
  zstreamer/
  qcap-demos/
```

## Dependency map

| Dependency | Source / expected location | How RX/TX uses it |
| --- | --- | --- |
| QCAP SDK | `qcap-dev`; output `qcap/build-qcap/_objs/xlnk2_arm64/` | `qcap2` decoder/encoder APIs and platform interfaces |
| zstreamer | `zstreamer`; output `build-xlnk2_arm64/src/` | Dante pipeline/coordinator shared libraries and headers |
| Audinate DAU Service 1.0.2 | Licensed Dante AV IP Core Software 2.1.1.3 source package; locally installed into `qcap-demos/3rdparty/` | `dauservice.h`, statically linked `libdauservice.a`; `libdbus-1` is linked dynamically |
| Platform dependencies | Builder image plus its AArch64 sysroot | ALLEGRO2, DRM, ALSA (RX), DBus, JSON-C, and the cross compiler |

RX and TX are explicitly registered CMake targets in
`cmake/QcapTargets.cmake`. Both require `ZZLAB`, QCAP, ALLEGRO2, DAUSERVICE,
DRM, and zstreamer; RX additionally requires ALSA. CMake deliberately links
DAUSERVICE statically (`-Wl,-Bstatic -ldauservice`) and expects its header and
archive under the repo-local `3rdparty/include` and `3rdparty/lib` search roots.
The pinned `qcap-build:xlnk2_arm64-base` image does not currently ship
`libdauservice.a`; provision the repo-local copy rather than relying on a
library left in an image or in another clone.

## 1. Provision DAU Service from the licensed source package

The current workspace package is:

```text
/mnt/rd2/RD2.EMBEDDED/qcap.linux/dante/ipcore_2.1.1.3/dante_av_ip_core_software.2.1.1.3.tgz
```

The outer package contains
`libdauservice/libdauservice_1.0.2.tar.gz`. Obtain this approved package
through the project's authorized artifact source if it is not present at that
path; do not expect it, or the resulting binaries, to come from `git clone`.

From the `qcap-demos` root, extract the source into a temporary directory under
`$HOME` (the Docker helper mounts `$HOME` into the builder):

```bash
set -euo pipefail
DAU_IPCORE_TARBALL=/mnt/rd2/RD2.EMBEDDED/qcap.linux/dante/ipcore_2.1.1.3/dante_av_ip_core_software.2.1.1.3.tgz
test -r "$DAU_IPCORE_TARBALL"
mkdir -p "$HOME/.cache"
DAU_WORK=$(mktemp -d "$HOME/.cache/qcap-demos-dauserve.XXXXXX")
tar -xOf "$DAU_IPCORE_TARBALL" \
  ./libdauservice/libdauservice_1.0.2.tar.gz | tar -xz -C "$DAU_WORK"
DAU_SRC="$DAU_WORK/libdauservice"
test "$(cat "$DAU_SRC/VERSION")" = "1.0.2"
```

Build the static archive with the same AArch64 SDK as the demos, then install
only the consumer header and archive into the ignored local dependency tree:

```bash
./scripts/docker-run.sh qcap-build:xlnk2_arm64-base \
  bash -lc ". /opt/sc6f0/environment-setup-cortexa72-cortexa53-xilinx-linux && make -C '$DAU_SRC' libdauservice.a"

install -Dm644 "$DAU_SRC/dauservice.h" 3rdparty/include/dauservice.h
install -Dm644 "$DAU_SRC/libdauservice.a" 3rdparty/lib/libdauservice.a
```

Do not copy a library from a host-native build: the archive must contain
AArch64 objects. This procedure uses the source package as the authority and
does not depend on an old, locally modified `3rdparty/` copy.

## 2. Build QCAP and zstreamer

Build dependencies in order so zstreamer and demos consume the matching
SC6F0-enabled QCAP output:

```bash
cd ~/docker/qcap-dev
./scripts/docker-run.sh qcap-build:xlnk2_arm64-base \
  ./build-qcap.sh xlnk2_arm64 all BUILD_SC6F0=ON

cd ~/docker/zstreamer
./scripts/build.sh xlnk2_arm64
```

Expected outputs:

```text
~/docker/qcap-dev/qcap/build-qcap/_objs/xlnk2_arm64/include/
~/docker/qcap-dev/qcap/build-qcap/_objs/xlnk2_arm64/lib/libqcap.so
~/docker/zstreamer/build-xlnk2_arm64/src/libzstreamer.so
~/docker/zstreamer/build-xlnk2_arm64/src/libzstreamer-elements.so
```

## 3. Build the two applications

From a fresh `qcap-demos` clone, at the repository root:

```bash
./scripts/docker-run.sh qcap-build:xlnk2_arm64-base \
  env BUILD_SYSTEM=cmake \
      QCAP_HOME="$HOME/docker/qcap-dev/qcap/build-qcap/_objs/xlnk2_arm64" \
      ZSTREAMER_ROOT="$HOME/docker/zstreamer" \
      CMAKE_ARGS_EXTRA="-DBUILD_SC6F0=ON" \
  ./build.sh xlnk2_arm64 sc6f0-dante-rx sc6f0-dante-tx
```

The two outputs are:

```text
_objs/xlnk2_arm64/bin/sc6f0-dante-rx
_objs/xlnk2_arm64/bin/sc6f0-dante-tx
```

Build either target individually by passing only its target name. Use the exact
CMake target names above (not `*-test`). When the sibling repositories are in
nonstandard locations, pass their actual paths through `QCAP_HOME` and
`ZSTREAMER_ROOT`; `ZSTREAMER_ROOT` is the repository root, not its build output
directory.

## Clean-clone checks and troubleshooting

Before building, verify the required inputs:

```bash
test -f 3rdparty/include/dauservice.h
test -f 3rdparty/lib/libdauservice.a
test -f "$HOME/docker/qcap-dev/qcap/build-qcap/_objs/xlnk2_arm64/include/qcap2.h"
test -f "$HOME/docker/qcap-dev/qcap/build-qcap/_objs/xlnk2_arm64/lib/libqcap.so"
test -f "$HOME/docker/zstreamer/build-xlnk2_arm64/src/libzstreamer.so"
test -f "$HOME/docker/zstreamer/build-xlnk2_arm64/src/libzstreamer-elements.so"
```

- Missing `dauservice.h` or `libdauservice.a`: repeat step 1. These are local,
  ignored artifacts and are not restored by refreshing the Git clone.
- Missing `libqcap.so` or `qcap2.h`: build QCAP with `BUILD_SC6F0=ON` as in
  step 2; qcap-demos consumes QCAP and must not build it internally.
- Missing zstreamer libraries: build the sibling zstreamer tree. The demos
  build does not rebuild zstreamer internally.
- If a CMake cache points at an old workspace, run the documented `distclean`
  target for `xlnk2_arm64`, then repeat step 3 with the explicit paths above.
- Successful cross-compilation is compile/link evidence only. Deploy and run
  hardware tests separately on the named SC6F0 board.
