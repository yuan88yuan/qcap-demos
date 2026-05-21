#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
DOCKERFILE="${REPO_ROOT}/docker/Dockerfile.toolchain-template"
BUILD_CONTEXT="${REPO_ROOT}/docker"

usage() {
    cat <<'EOF'
Usage:
  ./scripts/docker-build.sh <base-image> [image-tag]

Examples:
  ./scripts/docker-build.sh my/toolchain:xlnk2
  ./scripts/docker-build.sh my/toolchain:xlnk2 qcap-dev:xlnk2
EOF
}

if [[ $# -lt 1 || $# -gt 2 ]]; then
    usage
    exit 1
fi

if ! command -v docker >/dev/null 2>&1; then
    echo "Error: docker is not installed or not in PATH." >&2
    exit 1
fi

if [[ ! -f "${DOCKERFILE}" ]]; then
    echo "Error: Dockerfile template not found at ${DOCKERFILE}" >&2
    exit 1
fi

if [[ ! -d "${BUILD_CONTEXT}" ]]; then
    echo "Error: Build context directory not found at ${BUILD_CONTEXT}" >&2
    exit 1
fi

BASE_IMAGE="$1"
IMAGE_TAG="${2:-qcap-dev:${USER:-builder}}"
USERNAME="${USER:-builder}"
UID_VALUE="$(id -u)"
GID_VALUE="$(id -g)"

BUILD_CMD=(
    docker build
    --build-arg "BASE_IMAGE=${BASE_IMAGE}"
    --build-arg "USERNAME=${USERNAME}"
    --build-arg "UID=${UID_VALUE}"
    --build-arg "GID=${GID_VALUE}"
    -t "${IMAGE_TAG}"
    -f "${DOCKERFILE}"
    "${BUILD_CONTEXT}"
)

echo "Building image: ${IMAGE_TAG}"
echo "Base image: ${BASE_IMAGE}"
printf 'Running:'
printf ' %q' "${BUILD_CMD[@]}"
printf '\n'

"${BUILD_CMD[@]}"
