#!/usr/bin/env sh
# normalize-xdk.sh -- lowercase every filename under an XDK tree.
#
# The Microsoft XDK ships with mixed-case filenames (XTL.H, D3d8.h, ...).
# macOS APFS is case-insensitive so it doesn't matter there, but Linux
# (and case-sensitive APFS) will fail to resolve `#include <xtl.h>` when
# the file on disk is `XTL.H`. Run this once after dropping your XDK
# files in to make lookups deterministic across platforms.
#
# Usage: tools/normalize-xdk.sh [directory]   (default: xdk)

set -eu

DIR="${1:-xdk}"

if [ ! -d "$DIR" ]; then
    echo "normalize-xdk: not a directory: $DIR" >&2
    echo "usage: $0 [directory]" >&2
    exit 1
fi

renamed=0
skipped=0

# -depth processes entries bottom-up so we rename files before their
# parent directories. The two-step rename via a unique tmp name works
# on case-insensitive filesystems too (where `mv XTL.h xtl.h` is a no-op).
find "$DIR" -depth | while IFS= read -r path; do
    [ "$path" = "$DIR" ] && continue

    base=$(basename "$path")
    parent=$(dirname "$path")
    lower=$(printf '%s' "$base" | tr '[:upper:]' '[:lower:]')

    [ "$base" = "$lower" ] && continue

    tmp="$parent/.oxdk-normalize.$$"
    target="$parent/$lower"

    mv -- "$path" "$tmp"

    # After moving the source out of the way, if the target name still
    # exists it's a genuine separate file (only possible on case-sensitive
    # filesystems that already have both names). Bail out on that entry.
    if [ -e "$target" ]; then
        echo "normalize-xdk: collision, leaving alone: $path" >&2
        mv -- "$tmp" "$path"
        skipped=$((skipped + 1))
        continue
    fi

    mv -- "$tmp" "$target"
    echo "  $path -> $target"
    renamed=$((renamed + 1))
done

echo "normalize-xdk: done ($DIR)"
