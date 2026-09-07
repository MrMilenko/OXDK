# OXDK + libSDLx: sdl_test

Minimal SDL 1.2 (Xbox port) smoke test. Builds against pristine HyperEye
libSDLx (https://github.com/HyperEye/SDLx) vendored at
`../../third-party/libSDLx/` using OXDK's clang and lld-link toolchain.

If the produced `default.xbe` displays 8 vertical color bars on Xbox for
about 5 seconds and then returns to the dashboard, your OXDK and libSDLx
setup is working.

## Build

```sh
make
make XBE_MODE=DEBUG
make clean
```

The build expects:

* OXDK at `~/OXDK` (override with `OXDK_DIR` if elsewhere).
* XDK libs at `$(OXDK_DIR)/xdk/lib/` and headers at `$(OXDK_DIR)/xdk/include/`.
* clang and lld-link on `PATH`. Homebrew LLVM works; the Makefile defaults
  to `/opt/homebrew/opt/llvm`. Override `LLVM_PREFIX` if your install is
  elsewhere.
* libSDLx cloned into `$(OXDK_DIR)/third-party/libSDLx/`:

```sh
cd ~/OXDK
mkdir -p third-party
git clone https://github.com/HyperEye/SDLx.git third-party/libSDLx
```

## Files

`sdl_test.cpp` is the test program. It calls SDL_Init, SDL_SetVideoMode,
SDL_CreateRGBSurface, fills 8 vertical color bars in a 24bpp BGR source,
blits to the 16bpp screen, calls SDL_UpdateRect, sleeps 5 seconds, frees
the source, calls SDL_Quit, and relaunches the dashboard.

`xbox_platform.c` provides shims libSDLx expects from the C runtime but
the MS XDK CRT does not actually ship: `stat`, `strdup`, `SDL_CDROMInit`,
`SDL_CDROMQuit`. Copy this file into your own SDL on OXDK project. It
uses the real `<sys/stat.h>` `struct stat` layout, which matters; see the
notes below.

`xbox_preamble.h` is force included before every source file. It pulls
in the NT kernel header chain that libSDLx's `xtl.h` needs. Copy this
file too if you start your own SDL project.

`Makefile` compiles every libSDLx .c file alongside the sample and links
one XBE. No separate libSDLx.lib is produced; the build is monolithic by
design so newcomers can read the whole flow in one file.

## Starting your own SDL on OXDK project

1. Copy `xbox_platform.c` and `xbox_preamble.h` into your project.
2. Copy this Makefile and replace `APP_SRCS = sdl_test.cpp` with your own
   source list. Keep the `CFLAGS_SDL`, `LIBSDLX_DIR`, and `SDL_CORE_SRCS`
   blocks as they are.
3. Make sure `xbox_preamble.h` is force included via
   `-include $(CURDIR)/xbox_preamble.h`.
4. Link `xbox_platform.c` into your binary so libSDLx's references to
   `stat`, `strdup`, `SDL_CDROMInit`, and `SDL_CDROMQuit` resolve.

## Toolchain notes

The Xbox MSVC compiler defaults to stdcall calling convention. clang
defaults to cdecl on `i386-pc-windows-msvc`. The Makefile passes
`-Xclang -fdefault-calling-conv=stdcall` to match. Without that flag,
kernel imports like `IoCreateSymbolicLink` will not resolve at link time
(you will see `__imp__SomeFunction@8` undefined).

Two `io.h` files exist in the XDK include search path. The CRT one at
`public/sdk/inc/crt/io.h` provides POSIX style `_open` and `_read`, and
is found first. The NT kernel one at `private/ntos/inc/io.h` provides
`IoCreateSymbolicLink`. If you need the NT functions, forward declare
them yourself. The sdl_test sample does not mount drives so it does not
need them, but most real applications will.

The `stat` shim in `xbox_platform.c` uses the real `<sys/stat.h>`
`struct stat` definition rather than a local two field struct. The real
struct has `st_mode` at byte offset 6, after `_dev_t st_dev` and
`_ino_t st_ino`. Writing `st_mode` to a local struct at offset 0 then
having callers read at offset 6 silently breaks every "is this a
directory?" check. The sample does not exercise this directly but the
shim is set up correctly for any caller that does.

## Attribution

libSDLx is the work of Sam Lantinga and contributors, distributed under
the LGPL. The Xbox port lives at https://github.com/HyperEye/SDLx. This
example treats it as an unmodified third party dependency. No patches
are applied to libSDLx itself; everything Xbox toolchain specific lives
in this sample's Makefile and shim files.
