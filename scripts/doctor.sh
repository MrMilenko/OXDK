#!/usr/bin/env sh
# Report what OXDK found, and what is missing, for both consoles.
#
# Usage: scripts/doctor.sh

ROOT=$(cd "$(dirname "$0")/.." && pwd)

say() { printf '  %-22s %s\n' "$1" "$2"; }

# Ask make, so this reports exactly what a build would use.
ask() {
    make -f - <<MK 2>/dev/null
OXDK_DIR := $ROOT
include $ROOT/common/toolchain.mk
all:
	@echo "$1"
MK
}

echo "OXDK"
echo
echo "Host"
say "system" "$(uname -s) $(uname -m)"
echo
echo "Compiler"

OXDK_LLVM=$(ask "\$(OXDK_LLVM)")
if [ -n "$OXDK_LLVM" ]; then
    say "patched LLVM" "$OXDK_LLVM"
else
    say "patched LLVM" "not found, run scripts/build-llvm.sh"
fi

for t in LLD_LINK LLD OBJCOPY LLVM_AR LLVM_NM; do
    v=$(ask "\$($t)")
    case "$v" in
        /*) say "$(echo $t | tr 'A-Z_' 'a-z-')" "$v" ;;
        *)  found=$(command -v "$v" 2>/dev/null)
            say "$(echo $t | tr 'A-Z_' 'a-z-')" "${found:-$v (not found)}" ;;
    esac
done

libcxx=$(ask "\$(OXDK_LIBCXX_DIR)")
say "libc++ headers" "${libcxx:-not found}"

echo
echo "Xbox"
xclang=$(ask "\$(call oxdk-tool,clang)")
case "$xclang" in
    /*) : ;;
    *) xclang=$(command -v "$xclang" 2>/dev/null) ;;
esac
if [ -n "$xclang" ] && "$xclang" -target i386-pc-windows-msvc -x c -c /dev/null -o /dev/null 2>/dev/null; then
    say "x86 compiler" "$xclang"
elif [ -n "$xclang" ]; then
    say "x86 compiler" "$xclang has no x86 backend"
else
    say "x86 compiler" "not found"
fi
if [ -f "$ROOT/xbox/xdk/lib/xboxkrnl.lib" ]; then
    say "XDK" "$ROOT/xbox/xdk"
else
    say "XDK" "missing, copy your XDK into xbox/xdk"
fi

echo
echo "Xbox 360"
if [ -n "$OXDK_LLVM" ] && [ -x "$OXDK_LLVM/bin/clang" ]; then
    if "$OXDK_LLVM/bin/clang" -target powerpc-unknown-none-elf \
        -Xclang -target-feature -Xclang +xenon-abi \
        -x c -c /dev/null -o /dev/null 2>/dev/null; then
        say "xenon-abi" "supported"
    else
        say "xenon-abi" "clang found but the patches are missing"
    fi
else
    say "xenon-abi" "no patched clang"
fi
# XDK_DIR only counts if it looks like a 360 XDK. In an Xbox shell it points at
# the other console's, so fall back to the search either way.
xdk360=""
if [ -n "$XDK_DIR" ] && [ -f "$XDK_DIR/lib/xbox/xboxkrnl.lib" ]; then
    xdk360=$XDK_DIR
else
    xdk360=$(ask "\$(OXDK_XBOX360_XDK)")
fi
if [ -n "$xdk360" ] && [ -f "$xdk360/lib/xbox/xboxkrnl.lib" ]; then
    say "XDK" "$xdk360"
else
    say "XDK" "set XDK_DIR to an extracted Xbox 360 XDK"
fi
echo
