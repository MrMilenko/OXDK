# Original Xbox

Builds an XBE that a modded console will load. You supply the Xbox XDK; OXDK
ships no Microsoft code.

## Setting up

Any clang with an x86 backend works, including the one already on your system.
`./oxdk build-llvm` also produces one.

Copy your XDK into `xbox/xdk`:

```
xbox/xdk/include/     the XDK headers
xbox/xdk/lib/         the XDK .lib files
```

On a case sensitive filesystem, lowercase the tree first. The headers include
each other with inconsistent case:

```sh
xbox/tools/normalize-xdk.sh xbox/xdk
```

Then check it:

```sh
./oxdk doctor
```

## Building

```sh
make -C xbox/samples/d3d/hello
make -C xbox/samples/libcxx/cxx17_hello
```

Output lands in `bin/default.xbe`.

## A project

```make
OXDK_DIR  = /path/to/OXDK
XBE_TITLE = mygame
XBE_MODE  = RETAIL
SRCS      = main.cpp render.cpp
include $(OXDK_DIR)/oxdk.mk
```

`OXDK_TARGET` defaults to `xbox`, so it can be left out. `Makefile.template`
in `xbox/` is a starting point. The variables are documented at the top of
`xbox/xbox.mk`.

Useful ones:

- `XBE_MODE`, `DEBUG` or `RETAIL`.
- `XBE_LIMIT64MB`, `yes` by default. `no` lets a title use all 128 MB on an
  upgraded console or devkit. Stock hardware has 64 MB either way.
- `XBE_TITLEIMAGE`, a `$$XTIMAGE` XPR embedded as the dashboard icon.
- `OXDK_LIBCXX = 1`, use libc++ instead of the XDK's C++ library.

### Driving it yourself

`xbox/bin/oxdk-cc`, `oxdk-cxx` and `oxdk-link` wrap the flags. Or set them
directly:

```sh
clang++ -target i386-pc-windows-msvc -march=pentium3 \
    -fms-extensions -fms-compatibility -fms-compatibility-version=13.10 \
    -fno-rtti -fno-exceptions \
    -Xclang -fdefault-calling-conv=stdcall \
    -c -o main.obj main.cpp
```

Then `lld-link`, then `xbox/tools/cxbe/cxbe` to wrap the PE as an XBE.

## Three flags that matter

`-Xclang -fdefault-calling-conv=stdcall`. XDK projects compile with MSVC's
`/Gz`, making `__stdcall` the default for every function. clang defaults to
`__cdecl`. Without this, cross module calls corrupt the stack, often several
calls before anything crashes.

`xdk_compat.h`. The XDK headers assume MSVC's include ordering for NT kernel
types. The shim defines them before the XDK headers try to, avoiding
redefinition conflicts.

cxbe marks every XBE section executable and reads library versions from the
PE's `.XBLD` section rather than using a placeholder. Both are needed for XDK
linked binaries.

## C++

The XDK's C++ library is C++98 and its headers predate the language most code
is written in now. `OXDK_LIBCXX = 1` uses libc++ instead, with the headers
taken from an LLVM source tree.

RTTI is on: `libcmt` ships `__RTDynamicCast`, `__RTtypeid` and
`__RTCastToVoid`, so `dynamic_cast` and `typeid` resolve through the MSVC
runtime. Exceptions are off: clang emits the v3 EH personality
(`__CxxFrameHandler3`) and the MSVC 7.1 era CRT ships only v1.

The XDK CRT is C95, so `snprintf`, `vsnprintf` and the C99 float math
overloads come from `xbox/oxdk/libcxx-shim`, which is on the include path for
C as well as C++.

## Limits

- Modded consoles only. XBE section digests and the signature are zeroed, so
  stock retail consoles reject them. Softmod, TSOP and modchip all work.
- Debug D3D libraries (`d3d8d.lib`) hit assert breakpoints with a debugger
  attached. Use `d3d8.lib`.
- MSVC 7.1 leaks for-init variables into the enclosing scope
  (`/Zc:forScope-`). clang follows the standard, so porting older XDK code can
  mean hoisting declarations above the loop.

## Asset tools

`xbox/tools/xbx` converts between Xbox XPR0 (`.xbx`) textures and PNG, in Go
and in Python. See its README.

## Credits

cxbe comes from [NXDK](https://github.com/XboxDev/nxdk), whose work proving
clang and lld-link could target the Xbox is what made this possible. OXDK takes
a different approach, using the original XDK libraries rather than replacing
them.

The libraries OXDK builds against are other people's work:

- SDL, from [libsdl.org](https://www.libsdl.org/).
- libSDLx, the SDL 1.2 Xbox port by Lantus, vendored from
  [HyperEye/SDLx](https://github.com/HyperEye/SDLx). It bundles its own zlib,
  freetype and vorbis.
- RXDK-SDL2x and RXDK, from [Team Resurgent](https://github.com/Team-Resurgent).
- libc++, from the [LLVM project](https://github.com/llvm/llvm-project).
