#!/usr/bin/env bash

set -euo pipefail

usage() {
    cat <<'EOF'
Usage:
  ./scripts/docker-run-toolchain.sh <image-tag> [command ...]

Examples:
  ./scripts/docker-run-toolchain.sh qcap-dev:xlnk2_arm64
  ./scripts/docker-run-toolchain.sh qcap-dev:xlnk2_arm64 bash
  ./scripts/docker-run-toolchain.sh qcap-dev:xlnk2_arm64 ./build-qcap.sh xlnk2_arm64 install
EOF
}

if [[ $# -lt 1 ]]; then
    usage
    exit 1
fi

if ! command -v docker >/dev/null 2>&1; then
    echo "Error: docker is not installed or not in PATH." >&2
    exit 1
fi

IMAGE_TAG="$1"
shift || true

HOST_USER="${USER:-builder}"
HOST_HOME="${HOME:-/home/${HOST_USER}}"
HOST_UID="$(id -u)"
HOST_GID="$(id -g)"
WORK_DIR="$(pwd)"

if [[ ! -d "${HOST_HOME}" ]]; then
    echo "Error: host home directory not found: ${HOST_HOME}" >&2
    exit 1
fi

RUN_CMD=(
    docker run --rm
)

if [[ -t 0 && -t 1 ]]; then
    RUN_CMD+=( -it )
else
    RUN_CMD+=( -i )
fi

RUN_CMD+=(
    --user "${HOST_UID}:${HOST_GID}"
    -e "USER=${HOST_USER}"
    -e "HOME=${HOST_HOME}"
    -e "TERM=${TERM:-xterm-256color}"
    -v "${HOST_HOME}:${HOST_HOME}"
    -v "${WORK_DIR}:${WORK_DIR}"
    -w "${WORK_DIR}"
    "${IMAGE_TAG}"
)

if [[ $# -gt 0 ]]; then
    RUN_CMD+=("$@")
else
    RUN_CMD+=(bash)
fi

echo "Image: ${IMAGE_TAG}"
echo "User: ${HOST_USER} (${HOST_UID}:${HOST_GID})"
echo "Home mount: ${HOST_HOME}:${HOST_HOME}"
echo "Workdir: ${WORK_DIR}"
printf 'Running:'
printf ' %q' "${RUN_CMD[@]}"
printf '\n'

"${RUN_CMD[@]}"
