# Xbox 360

Builds a XEX2 that an RGH or devkit console will load. You supply the Xbox 360
XDK; OXDK ships no Microsoft code.

## Setting up

Build the compiler, which must carry the Xenon ABI patches:

```sh
./oxdk build-llvm
```

Then point `XDK_DIR` at an extracted XDK, the directory holding
`lib/xbox/xboxkrnl.lib`. `~/xdk360/XDK` and `~/xdk360-extract/sdk/XDK` are found
without being told.

```sh
export XDK_DIR=$HOME/xdk360/XDK
./oxdk doctor
```

### Extracting the XDK on a Unix host

The installer is an InstallShield self extractor wrapping 16 CAB files. 7-Zip
unpacks only the first and reports one vague warning, which is why the headers
and libraries appear to be missing. Carve all sixteen out by scanning for the
`MSCF` signature and reading each cabinet's `cbCabinet` field, then run
`cabextract` on each.

The XDK's filenames are mixed case and its headers include each other with
inconsistent case. `xbox/tools/normalize-xdk.sh` lowercases a tree.

## Building

```sh
make -C xbox360/samples/abitest    # checks the ABI and shows the results
make -C xbox360/samples/d3dclear   # clears the screen
make -C xbox360/samples/title      # the skeleton of a title
```

`abitest` is the one to run first on a new setup. It formats 64 bit integers
and a double with Microsoft's own `sprintf` and puts each result on screen next
to the value it should have, so a broken toolchain reads as a wrong number
rather than a crash. Press A, B or Start to leave it.

## A project

```make
OXDK_DIR    := /path/to/OXDK
OXDK_TARGET := xbox360
SRCS        := main.c
XEX_TITLE   := mygame
XDK_LIBS    := xapilib,d3d9,libcMT
include $(OXDK_DIR)/oxdk.mk
```

`xbox360/xbox360.mk` sets the MS compatibility flags the XDK headers need,
translates the XDK libraries to ELF, links, resolves the imports the console
provides at load time, and wraps the result as a XEX. Its variables are
documented at the top of that file.

Set `XENON_ABI := 1` unless you have a reason not to. Without it the compiler
uses PowerPC SysV, which does not match the XDK, and `va_list` is the wrong
type.

## C++

`.cpp` and `.cc` build like C. The C++ library needs a decision.

Microsoft's, in `libcpMT.lib`, cannot be linked against: clang mangles the
Itanium way and that library holds the same entities under MSVC's mangling, so
a reference never meets its definition. Templates and inline code are
unaffected, since they land in your own objects, which is why `std::map` and
`std::vector` work and iostreams do not.

`OXDK360_LIBCXX := 1` uses libc++ instead, so every entity is one you compiled.
That covers containers, strings, algorithms and the rest of the header only
library.

Iostreams, locale and streambuf are the compiled part of libc++, and there is
no prebuilt libc++ for this target. `OXDK360_LIBCXX_LIB := 1` builds one from
libc++'s sources, with `LIBCXX_SRC` pointing at the `libcxx` directory of an
LLVM checkout of the same release as the headers.

Two limits:

- No exceptions. Titles build `-fno-exceptions`. clang emits an EH personality
  the XDK CRT does not ship. `try` and `throw` do not compile.
- No threads in libc++. `std::thread` and `std::mutex` are off, because
  libc++'s Win32 backend uses fiber local storage the console has no header
  for. Call `CreateThread` through the XDK.

## Floating point in printf

The XDK CRT only links its floating point `printf` support when something
references `_fltused`. MSVC emits that reference from every object that touches
floating point; clang emits nothing, so `printf("%f")` would reach a stub that
prints `floating point support not loaded` and calls `KeBugCheck`.

`oxdklink` forces the symbol into every link, so this works without anything in
your source. It costs a few KB in a title that never formats a float.

## Running it

`xbox360/tools/xbdm` talks to XBDM on port 730.

```sh
python3 xbox360/tools/xbdm/xbdm.py <host> deploy default.xex 'Hdd1:\mygame\default.xex'
python3 xbox360/tools/xbdm/xbdm.py <host> cmd 'magicboot title="Hdd1:\mygame\default.xex" directory="Hdd1:\mygame"'
python3 xbox360/tools/xbdm/xbdm.py <host> listen
python3 xbox360/tools/xbdm/xbdm.py <host> screenshot shot.png
```

## How the toolchain is shaped

There is no PowerPC COFF in LLVM. Xenon objects are COFF with machine `0x01F2`;
`llvm-readobj` will not open them and `lld-link` has no PPC machine type.
`clang -target powerpc-unknown-windows-msvc` ignores the `windows-msvc` part
and emits ELF. So the path is clang to PowerPC ELF, `ld.lld`, then a PE and XEX
wrapper, with a translator so the XDK's static libraries can be linked at all.

Imports are not PE imports. They live in the XEX header as ordinal patch lists
and the loader writes addresses into the image. See [XEX2.md](XEX2.md).

The calling convention matches nothing LLVM implements, which is why it is a
backend patch rather than a target triple. See
[../xbox360/llvm/README.md](../xbox360/llvm/README.md).

## Tools

- `xbox360/tools/cxex`: builds a XEX2 from an image and a manifest.
- `xbox360/tools/oxdklink`: links, resolves kernel and xam imports, writes thunks.
- `xbox360/tools/coff2elf`: translates the XDK's COFF libraries to ELF.
- `xbox360/tools/xbdm`: deploy, launch, debug output, screenshots.
- `xbox360/tools/xexutil`: dump, unpack and describe XEX files and COFF objects.
- `xbox360/tools/abitest`: check a compiler and compiled objects against the ABI.
