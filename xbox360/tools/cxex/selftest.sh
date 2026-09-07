#!/bin/sh
# Round-trip cxex against a XEX built by Microsoft's imagexex.
#
# demofixer.xex ships in the XDK unencrypted and basic-compressed, so it can be unpacked,
# described as a manifest, and rebuilt. The rebuilt image must come back byte for byte and
# the import table must match exactly, next_import_digest chain included. The header digest
# must also seal the header region cxex produced.
#
# usage: selftest.sh /path/to/XDK

set -e

XDK="${1:-$XDK_DIR}"
if [ -z "$XDK" ] || [ ! -f "$XDK/bin/xbox/demofixer.xex" ]; then
    echo "selftest: need a path to an extracted XDK containing bin/xbox/demofixer.xex" >&2
    exit 2
fi

here=$(cd "$(dirname "$0")" && pwd)
util="$here/../xexutil"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

ref="$XDK/bin/xbox/demofixer.xex"

python3 "$util/xexunpack.py" "$ref" "$work/ref.image" > /dev/null
python3 "$util/xexmanifest.py" "$ref" "$work/ref.image" > "$work/ref.manifest"
"$here/cxex" -m "$work/ref.manifest" -i "$work/ref.image" -o "$work/out.xex" > /dev/null
python3 "$util/xexunpack.py" "$work/out.xex" "$work/out.image" > /dev/null

if cmp -s "$work/ref.image" "$work/out.image"; then
    echo "ok    image round-trips byte for byte"
else
    echo "FAIL  image differs from the original"
    exit 1
fi

python3 "$here/checks.py" "$ref" "$work/out.xex"

echo "selftest passed"
