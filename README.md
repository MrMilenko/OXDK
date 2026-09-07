# OXDK

Build Microsoft XDK projects for the original Xbox and the Xbox 360 on macOS
and Linux, with clang. No Windows, no virtual machine.

Five years of reverse engineering Windows only tooling from a Mac, so that
nobody else has to.

This is not a replacement for the XDK. You supply your own copy of Microsoft's
SDK. OXDK is the toolchain glue that lets a Unix host use it.

| console | output | status |
| --- | --- | --- |
| Original Xbox | XBE | builds and runs, C and C++ including libc++ |
| Xbox 360 | XEX2 | builds and runs, using the Xbox 360 calling convention |

## Showcase

OXDK compiles the Xbox side of [Theseus](https://github.com/MrMilenko/Theseus), the reverse-engineered original Xbox Dashboard engine. The same engine powers UIX Desktop on macOS, Linux, and Windows from the shared repo. Here it is on real hardware:

<img src="docs/screenshots/theseus_xbox_build.png" width="480">

The samples in this repo are all built with OXDK and booted on a console too. Left to right: the OXDK Summer 2026 Demo (SDL 2 video and input with a block breaker mini game in libc++), then RXDK-SDL2x plasma, starfield, and testgamecontroller.

<img src="docs/screenshots/oxdk_summer_demo_2026.png" width="240"> <img src="docs/screenshots/rxdk-sdl2x_plasma_example.png" width="240">
<img src="docs/screenshots/rxdk-sdl2x_starfield_example.png" width="240"> <img src="docs/screenshots/rxdk-sdl2x_testgamecontroller_example.png" width="240">

Shots are framebuffer captures over XBDM from a debug Xbox.

## Opening OXDK

```sh
./oxdk           # a shell set up for the original Xbox
./oxdk 360       # a shell set up for the Xbox 360
./oxdk doctor    # report what was found and what is missing
```

`./oxdk` opens your own shell with the compiler, the XDKs and the tools already
on PATH. Type `exit` to leave it. If you would rather set up the shell you are
already in, source the same script:

```sh
. scripts/oxdk-env.sh 360
```

Nothing is required to be in a fixed place. Every path is searched for and
every one can be overridden from the environment. `./oxdk doctor` prints what
was found, so a setup problem says what it is instead of failing later in a
build.

## Getting set up

### 1. The compiler

Both consoles want clang. The Xbox 360 needs one carrying the Xenon ABI
patches, because stock clang cannot produce code that links against the 360
XDK. One build covers both consoles:

```sh
./oxdk build-llvm
```

That clones LLVM, applies the patches from `xbox360/llvm`, and builds clang and
lld with the PowerPC and X86 backends. It takes a while and wants about 20 GB.
When it finishes it is found automatically.

If you only care about the original Xbox, any clang with an x86 backend will
do, including the one your system already has.

### 2. The XDK

You supply this. OXDK ships no Microsoft code.

For the original Xbox, copy your XDK into `xbox/xdk`, so that
`xbox/xdk/lib/xboxkrnl.lib` exists.

For the Xbox 360, point `XDK_DIR` at an extracted 360 XDK, the directory
holding `lib/xbox/xboxkrnl.lib`. `~/xdk360/XDK` and `~/xdk360-extract/sdk/XDK`
are found without being told. Extracting one on a Unix host is described in
[docs/xbox360.md](docs/xbox360.md).

### 3. Build something

```sh
make -C xbox/samples/libcxx/cxx17_hello   # original Xbox, C++17 and libc++
make -C xbox360/samples/abitest           # Xbox 360, checks the ABI on screen
```

## Using it in your own project

Set `OXDK_DIR`, pick a console, list your sources, include `oxdk.mk`:

```make
OXDK_DIR    = /path/to/OXDK
OXDK_TARGET = xbox360
SRCS        = main.c
include $(OXDK_DIR)/oxdk.mk
```

`OXDK_TARGET` is `xbox` by default, so existing Xbox projects need no change.
The variables each console understands are documented at the top of
`xbox/xbox.mk` and `xbox360/xbox360.mk`.

## What is here

```
oxdk                the launcher
common/             toolchain discovery, shared libc++ headers
scripts/            build-llvm.sh, doctor.sh, oxdk-env.sh
xbox/               original Xbox: cxbe, libc++ support, samples, your XDK
xbox360/            Xbox 360: cxex, oxdklink, XBDM, LLVM patches, samples
docs/
  xbox.md           the original Xbox
  xbox360.md        the Xbox 360
  XEX2.md           the XEX2 format, from real files
  DEVELOPMENT.md    working on OXDK itself
```

## Where this came from

Five years of working ass backwards on macOS.

I stopped using Windows, and then decided to go after a toolchain that had only
ever run on it. The XDKs are Windows software through and through. They expect
Visual Studio, MSVC's calling conventions, MSVC's mangling, and a filesystem
that does not care about case. None of that was on the machine in front of me.

Doing it anyway meant learning the XBE and XEX2 formats out of the files
themselves rather than from documentation, working out Microsoft's calling
conventions by disassembling their own libraries, and writing the parts of the
compiler that were missing. Most of what is in here started as a question that
should have had an easy answer and did not.

It is not a clean room reimplementation and it is not a replacement for the
XDK. It is the glue that lets a Unix host use the real thing.

## License

Public domain. The tools, the patches and the build system are given to the
community to use, change and build on. Third party code in the tree keeps its
own license, and anything you build with OXDK is yours. See
[LICENSE](LICENSE).

Microsoft's XDK is not here and is not ours to give.
