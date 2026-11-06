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
| nonvolatile registers | r14-r31 whole (all 64 bits), f14-f31, v14-v31 | `__savegprlr_N` stores with `std`; xsdk, measured on hardware |
| 64 bit results | whole in r3 | xsdk, measured on hardware |
| aggregate results | 1, 2, 4 or 8 bytes in r3 (8 as one 64 bit value, a struct of one double too), others through a pointer in r3 | xsdk, measured on hardware |
| aggregate arguments | up to 8 bytes as bytes in one GPR and slot, never an FPR: 1, 2, 4 right justified, other sizes left justified | xsdk, measured on hardware |
| `char` and `short` arguments | not extended by the caller; the callee extends them | xsdk, measured on hardware |

xsdk is an MIT-licensed Xbox 360 toolchain whose author measured these on an RGH
console against code from Microsoft's compiler (its docs/HARDWARE.md, "The
console's calling convention, register by register" and "Calls between GCC and
MSVC").

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

`0004-PowerPC-Xenon-nonvolatile-GPRs-as-doublewords.patch` saves and restores
r14-r31 with `std` and `ld` of the whole register, callee-saved list and the
prologue's own frame and base pointer saves alike, in doubleword slots. Before
it a 32 bit subtarget saved them with `stw`, and every function of ours that
used one handed its caller the register back with the upper half zeroed. Our
code is called by Microsoft's at main, every thread entry point, every qsort
or bsearch comparator and any SDK callback, and Microsoft's code keeps 64 bit
values in r14-r31 across those calls.

`0005-PowerPC-Xenon-results-and-small-aggregates-as-Microsoft.patch` returns a
64 bit value whole in r3, on both sides of a call (through a stack slot, as in
patches 2 and 3), and has clang classify returns and arguments the way
Microsoft's compiler does: aggregates of 1, 2, 4 or 8 bytes returned in r3,
aggregates of up to 8 bytes passed as their bytes in one register, and `char`
and `short` arguments and results not assumed extended.

Together they are about 1,000 added lines across 15 files.

Patches 2 and 3 both need a 64 bit register on a 32 bit subtarget, which Xenon
has and LLVM otherwise gates on PPC64. Both work the same way: the value is
written to its slot and the whole doubleword is loaded back into the register,
because the core has no move between an FPR and a GPR and no way to build a 64
bit value from two 32 bit ones.

## What is not covered

- An aggregate argument over 8 bytes is still passed as a pointer to a copy.
  Microsoft's compiler passes its bytes across consecutive slots, starting on an
  even slot, with its own count of general registers. `tools/abitest/sdkguard.py`
  lists every such call into code the unit does not define (and every
  address-taken function of ours taking one), so a title can refuse them.
- An aggregate result over 8 bytes goes through a hidden pointer in r3 on both
  sides, but Microsoft's callee also returns that pointer in r3 and ours does
  not. Only a Microsoft caller of our function could notice.
- Vector registers: we build without AltiVec, so nothing touches v0-v31.
  `tools/abitest/nvcheck.py` checks for any vector instruction.
- Bitfields follow SysV layout unless the unit passes `-mms-bitfields`
  (`OXDK360_SDK_FLAGS` in xbox360.mk).

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

```sh
python3 xbox360/tools/abitest/nvcheck.py --qemu qemu-ppc64
python3 xbox360/tools/abitest/callcheck.py --qemu qemu-ppc64
```

run the nonvolatile register and the call cases under qemu's 64 bit user mode,
called from and calling into hand-written code that behaves as Microsoft's
does. Both also take `--llc` to try a backend change before clang is rebuilt,
and `--keep DIR` to build the programs on one machine and run them on another
(qemu user mode is a Linux program).
`tools/abitest/console` is the same test as a XEX, run against Microsoft's own
`sprintf`.

With the feature off, the patched compiler's output is byte identical to an
unpatched one of the same version, across fifteen target and optimization
combinations.

## License

The patches are public domain, like the rest of OXDK. They are changes to
LLVM, which is Apache-2.0 WITH LLVM-exception, and that governs the patched
result.
