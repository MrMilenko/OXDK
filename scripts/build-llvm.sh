#!/usr/bin/env sh
# Build the LLVM that OXDK uses, on macOS or Linux.
#
# The Xbox 360 needs a clang carrying the Xenon ABI patches, because stock
# clang cannot produce code that links against the 360 XDK. The original Xbox
# needs a clang that can target x86. This builds one compiler that does both,
# so there is nothing else to install.
#
# Usage:
#   scripts/build-llvm.sh [install-dir]
#
# Default install-dir is $HOME/oxdk-llvm. Takes a while and wants about 20 GB.

set -e

ROOT=$(cd "$(dirname "$0")/.." && pwd)
DEST=${1:-$HOME/oxdk-llvm}
LLVM_TAG=llvmorg-22.1.8

for tool in git cmake ninja; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "$tool is needed and was not found."
        echo
        echo "  macOS:          brew install $tool"
        echo "  Debian, Ubuntu: sudo apt install $tool"
        echo "  Fedora:         sudo dnf install $tool"
        exit 1
    fi
done

if ! command -v cc >/dev/null 2>&1 && ! command -v clang >/dev/null 2>&1; then
    echo "A host C compiler is needed to build LLVM and was not found."
    exit 1
fi

if [ ! -d "$DEST/.git" ]; then
    echo "Cloning LLVM $LLVM_TAG into $DEST"
    git clone --depth 1 -b "$LLVM_TAG" https://github.com/llvm/llvm-project.git "$DEST"
else
    echo "Using the LLVM already at $DEST"
fi

cd "$DEST"

for patch in "$ROOT"/xbox360/llvm/*.patch; do
    name=$(basename "$patch")
    if git apply --check "$patch" >/dev/null 2>&1; then
        echo "Applying $name"
        git apply "$patch"
    elif git apply --reverse --check "$patch" >/dev/null 2>&1; then
        echo "Already applied: $name"
    else
        echo "Could not apply $name. The tree may have been modified."
        exit 1
    fi
done

# PowerPC for the Xbox 360, X86 for the original Xbox, so one build serves both.
cmake -S llvm -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DLLVM_TARGETS_TO_BUILD='PowerPC;X86' \
    -DLLVM_ENABLE_PROJECTS='clang;lld' \
    -DLLVM_INCLUDE_TESTS=OFF

ninja -C build clang lld llvm-ar llvm-nm llvm-objcopy

echo
echo "Done. Point OXDK at it with:"
echo
echo "  export OXDK_LLVM=$DEST/build"
echo
echo "Or leave it at $HOME/oxdk-llvm and it will be found on its own."
