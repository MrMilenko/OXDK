#!/usr/bin/env bash
# Cross-compile xbx-convert for the common platforms. Run from this directory.
# Output: dist/xbx-convert-<os>-<arch>[.exe]
set -euo pipefail

cd "$(dirname "$0")"
mkdir -p dist
rm -f dist/xbx-convert-*

build() {
    local goos=$1 goarch=$2 ext=${3:-}
    local out="dist/xbx-convert-${goos}-${goarch}${ext}"
    echo "  -> ${out}"
    GOOS=$goos GOARCH=$goarch CGO_ENABLED=0 \
        go build -trimpath -ldflags="-s -w" -o "$out" .
}

echo "building xbx-convert..."
build linux   amd64
build linux   arm64
build darwin  amd64
build darwin  arm64
build windows amd64 .exe
build windows arm64 .exe

echo
ls -lh dist/
