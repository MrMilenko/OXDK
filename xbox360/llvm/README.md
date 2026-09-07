# Xenon ABI patches for LLVM

Xbox 360 code uses a calling convention LLVM does not implement. Stock clang
emits PowerPC SysV frames, which do not match what the XDK's libraries expect,
so calls between compiler output and Microsoft's code corrupt each other's
stacks. These patches add the convention behind a subtarget feature, so nothing
changes for any other target.

Written against LLVM 22.1.8, tag `llvmorg-22.1.8`, commit
`ca7933e47d3a3451d81e72ac174dcb5aa28b59d1`.

## The convention

Measured by disassembling `libcMT.lib` and reading the XDK's `vadefs.h`.

| rule | value | evidence |
| --- | --- | --- |
| linkage area | 16 bytes | frame layout in XDK objects |
| argument registers | r3 to r10, each with a reserved 8 byte slot | no caller writes below caller_sp+80 |
| first stack argument | `caller_sp+80` | 16 + 8x8, across 545 call sites |
| parameter slots | 8 bytes, scalars right justified | `_VA_ALIGN` and `_VA_IS_LEFT_JUSTIFIED` in `vadefs.h` |
| return address | word at `caller_sp-8` | `stw r12, -8(r1)`, 395 occurrences |
| callee saved GPRs | doublewords from `caller_sp-16` down | `std r31, -16(r1)`, 252 occurrences |
| floating point arguments | next free FPR, still consuming a slot | `D3DDevice_Clear` takes its seventh argument in r9, not r8 |
| 64 bit values | one 64 bit register and one slot | `_snprintf` homes r6 to r10 as doublewords |
| variadic arguments | homed into the caller's slots, `va_list` is one `char *` | `wsprintfA` homes r5 to r10 and points va_list at slot 3 |

The return address rule is why this cannot be a target triple. It sits in the
callee's own frame, where every other PowerPC ABI puts it in the caller's
linkage area, and LLVM does not otherwise reserve that word.

## The patches

`0001-PowerPC-add-the-Xenon-ABI.patch` adds the convention and the
`xenon-abi` subtarget feature.

`0002-PowerPC-Xenon-variadic-FP-in-GPR.patch` puts a variadic `double` in the
64 bit general register sharing its slot, as well as in an FPR. A variadic
callee homes that register over the slot without knowing the type, so a double
that exists only in an FPR is lost.

`0003-PowerPC-Xenon-64-bit-integers-in-one-register.patch` gives a 64 bit
integer one register and one slot. It arrives split into two 32 bit halves,
because i64 is not a legal type on a 32 bit subtarget, and without this it took
two slots and left every following argument one register out of place.

Together they are about 630 added lines across 13 files.

Patches 2 and 3 both need a 64 bit register on a 32 bit subtarget, which Xenon
has and LLVM otherwise gates on PPC64. Both work the same way: the value is
written to its slot and the whole doubleword is loaded back into the register,
because the core has no move between an FPR and a GPR and no way to build a 64
bit value from two 32 bit ones.

## What is not covered

64 bit return values. clang returns them as an r3:r4 pair:

```
bl  get64
mr  3, 4        # the low half comes from r4
```

Microsoft's convention for this is not established here. Every argument rule
above was measured from compiled code, and no call site returning a 64 bit
value by register was found in `libcMT.lib` to measure against. Until one is,
changing it would be a guess. A function returning `__int64` by value across
the boundary may be read wrongly; returning through a pointer is unaffected.

## Building

`./oxdk build-llvm` does this, including the X86 backend so the same compiler
also builds for the original Xbox. By hand:

```sh
git clone --depth 1 -b llvmorg-22.1.8 https://github.com/llvm/llvm-project.git oxdk-llvm
cd oxdk-llvm
for p in /path/to/OXDK/xbox360/llvm/*.patch; do git apply "$p"; done
cmake -S llvm -B build -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DLLVM_TARGETS_TO_BUILD='PowerPC;X86' \
      -DLLVM_ENABLE_PROJECTS='clang;lld' \
      -DLLVM_INCLUDE_TESTS=OFF
ninja -C build clang lld llvm-ar llvm-nm llvm-objcopy
```

Wants about 20 GB. Clone to `~/oxdk-llvm` and OXDK finds it, otherwise set
`OXDK_LLVM` to the build directory.

## Using it

Off by default. Enable it per compilation:

```sh
clang --target=powerpc-unknown-elf -Xclang -target-feature -Xclang +xenon-abi ...
```

`xbox360/xbox360.mk` does this when a project sets `XENON_ABI := 1`.

## Checking it

```sh
python3 xbox360/tools/abitest/abicheck.py --cc /path/to/clang --feature xenon-abi
```

checks the compiler against the rules above. `framecheck.py` checks compiled
objects for the two frame overlaps this ABI makes possible.
`tools/abitest/console` is the same test as a XEX, run against Microsoft's own
`sprintf`.

With the feature off, the patched compiler's output is byte identical to an
unpatched one of the same version, across fifteen target and optimization
combinations.

## License

The patches are public domain, like the rest of OXDK. They are changes to
LLVM, which is Apache-2.0 WITH LLVM-exception, and that governs the patched
result.
