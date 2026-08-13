#!/usr/bin/env bash

set -euo pipefail

usage() {
    cat <<EOF
Usage:
  $0 <platform> [--variant cuda|vaapi|none]

Examples:
  $0 xlnk2_arm64
  $0 ubuntu2004_x64 --variant cuda
  $0 ubuntu2004_x64 --variant vaapi

Options:
  --variant   Fetch a GPU-specific x64 directory and merge it over the base
              platform directory. The default is "none".
  -h, --help  Show this help message.

Environment:
  QCAP_3RDPARTY_REPO    Override the 3rdparty Git repository URL.
  QCAP_3RDPARTY_BRANCH  Override the repository branch (default: develop).
EOF
}

if [[ $# -lt 1 ]]; then
    usage >&2
    exit 1
fi

case "$1" in
    -h|--help)
        usage
        exit 0
        ;;
esac

PLATFORM="$1"
shift

VARIANT="none"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --variant)
            if [[ $# -lt 2 ]]; then
                echo "Error: --variant requires a value." >&2
                usage >&2
                exit 1
            fi
            VARIANT="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Error: Unknown option: $1" >&2
            usage >&2
            exit 1
            ;;
    esac
done

case "$VARIANT" in
    cuda|vaapi|none) ;;
    *)
        echo "Error: Invalid variant '$VARIANT'. Supported values: cuda, vaapi, none." >&2
        usage >&2
        exit 1
        ;;
esac

REPOSITORY_URL="${QCAP_3RDPARTY_REPO:-git@rd2.yuan.com.tw:Bill/qcap-dev-3rdparty.git}"
BRANCH="${QCAP_3RDPARTY_BRANCH:-develop}"

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd)"
DEST_ROOT="$PROJECT_ROOT/3rdparty"

FETCH_DIRS=("$PLATFORM")
if [[ "$PLATFORM" == *_x64 ]]; then
    PREFIX="${PLATFORM%_x64}"
    case "$VARIANT" in
        cuda)  FETCH_DIRS+=("${PREFIX}-cuda_x64") ;;
        vaapi) FETCH_DIRS+=("${PREFIX}-vaapi_x64") ;;
    esac
elif [[ "$VARIANT" != "none" ]]; then
    echo "Warning: Variant '$VARIANT' applies only to *_x64 platforms; fetching base platform only." >&2
fi

echo "Fetching platform: $PLATFORM"
echo "Variant: $VARIANT"
echo "From: $REPOSITORY_URL (branch: $BRANCH)"

TMP_DIR="$(mktemp -d)"
trap 'rm -rf -- "$TMP_DIR"' EXIT
CLONE_DIR="$TMP_DIR/repository"
MERGE_DIR="$TMP_DIR/merged"

# Fetch only the requested platform directories. Variant files are copied after
# base files so that variant-specific versions take precedence.
git clone --depth 1 --filter=blob:none --sparse --branch "$BRANCH" \
    "$REPOSITORY_URL" "$CLONE_DIR"
git -C "$CLONE_DIR" sparse-checkout set "${FETCH_DIRS[@]}"

if [[ ! -d "$CLONE_DIR/$PLATFORM" ]]; then
    echo "Error: Platform directory '$PLATFORM' was not found in the 3rdparty repository." >&2
    exit 1
fi

mkdir -p "$MERGE_DIR"
for dir in "${FETCH_DIRS[@]}"; do
    if [[ -d "$CLONE_DIR/$dir" ]]; then
        echo "Merging $dir -> ./3rdparty"
        cp -a "$CLONE_DIR/$dir/." "$MERGE_DIR/"
    else
        echo "Warning: Directory '$dir' was not found; skipping it." >&2
    fi
done

# Replace the previous platform contents only after fetching and merging have
# succeeded, preventing a failed download from deleting the current files.
rm -rf -- "$DEST_ROOT"
mkdir -p "$DEST_ROOT"
cp -a "$MERGE_DIR/." "$DEST_ROOT/"

echo "Done. Platform dependencies were merged into $DEST_ROOT."
