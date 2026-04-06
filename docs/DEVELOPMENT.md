# How We Got Here

A development log for OXDK, covering the discovery process, the failures, the fixes, and the edge cases that almost made us quit.

## The Problem

The Theseus project -- a decompilation of the original Xbox Dashboard -- was being developed on a Windows XP VM running Visual Studio .NET 2003. The build machine was a Thinkpad accessible via network share. Compiling meant saving files on macOS, waiting for the share to sync, switching to the VM, clicking Build, waiting, then FTPing the XBE to the Xbox for testing. A change-compile-test cycle took minutes.

The question was simple: can we cut Windows out entirely and compile Xbox code on macOS?

## The Discovery Phase

### Can clang target Xbox?

The Xbox CPU is a Pentium III running a stripped Windows 2000 kernel. MSVC 7.1 targets `i386` with the Windows MSVC ABI. Clang supports `-target i386-pc-windows-msvc`, which produces PE/COFF object files with MSVC-compatible name mangling and calling conventions.

The NXDK project had already proven that clang + lld-link could produce bootable Xbox executables -- but NXDK replaces the entire XDK runtime with open-source alternatives. We wanted to use the original Microsoft XDK libraries directly. Same libs the retail dashboard was linked against.

### First compile attempt

```
clang++ -target i386-pc-windows-msvc -march=pentium3 -c main.cpp
```

Immediate wall of errors. The XDK headers assume MSVC-specific include ordering. Types like `NTSTATUS`, `STRING`, `OBJECT_ATTRIBUTES` get defined by NT kernel headers (`ntdef.h`, `ntos.h`) which need to be included before the XDK's `xtl.h` touches `winbase.h`. MSVC's precompiled header system handles this implicitly. Clang doesn't have that luxury.

**Solution**: `xdk_compat.h`, force-included via `-include xdk_compat.h`. This pulls in the NT type definitions in the right order and sets include guards (`_WINNT_`, `_INTERLOCKED_DEFINED`) to prevent the XDK headers from redefining things.

### Linking

lld-link handles MSVC COFF libraries natively. Pointed it at the XDK `.lib` files and... it mostly worked. Except for kernel imports.

The Xbox kernel (`xboxkrnl.lib`) exports functions with stdcall decoration: `_HalReturnToFirmware@4`. Clang generates undecorated imports: `__imp__HalReturnToFirmware`. The linker can't match them.

**Solution**: `/alternatename` directives mapping undecorated names to their decorated equivalents:

```
/alternatename:__imp__HalReturnToFirmware=__imp__HalReturnToFirmware@4
```

One per kernel function. Tedious but mechanical -- count the parameter bytes, append `@N`.

### PE to XBE

Xbox executables are XBE files, not standard PE. NXDK's `cxbe` tool converts PE to XBE. Grabbed it, ran it, got an XBE. Copied to Xbox. Black screen.

## The Crash Investigation

### The stdcall catastrophe

First working XBE: boots, shows the splash screen, crashes immediately when any real code runs.

The Xbox XDK compiles everything with `/Gz` -- default `__stdcall` calling convention. This means the callee cleans the stack. Clang defaults to `__cdecl` where the caller cleans the stack. When clang-compiled code calls an XDK library function (stdcall), both sides think they own stack cleanup. The stack pointer drifts by 4 bytes per argument per call. It might survive one or two calls before memory corruption takes over.

This was the hardest bug to find because the symptoms are random. Sometimes it crashes in D3D init. Sometimes it gets further. Sometimes it corrupts a return address and jumps to garbage. No consistent repro.

**The fix**: `-Xclang -fdefault-calling-conv=stdcall`

One flag. That's it. Hours of debugging for one compiler flag.

**Critical detail**: This flag only goes in `CXXFLAGS`, not `CFLAGS`. C functions use explicit `WINAPI`/`WSAAPI` attributes for their calling convention. Forcing stdcall on C code can break things that expect cdecl (like variadic functions, which can't be stdcall).

### cxbe section flags

While debugging the stdcall issue, we also noticed that cxbe wasn't marking all XBE sections as executable. The Xbox kernel's section loader may need the executable flag set on data sections -- we're honestly not 100% sure this was causing problems independently, or if the stdcall fix was the real breakthrough. We made both changes during the same debugging session and never went back to isolate which one mattered.

The change is one line in `Xbe.cpp`: unconditionally set `bExecutable = true` on all section headers.

### cxbe library versions

Xbox subsystem initialization (D3D, DirectSound, XNet) checks the XBE's library version table to determine which subsystem version to activate. Stock cxbe creates a placeholder `CXBE0` entry. The Xbox kernel doesn't recognize it and doesn't initialize the subsystems properly.

**Fix**: Read the actual library version entries from the PE's `.XBLD` section (which the XDK linker embeds) instead of generating placeholders. Also correctly set `dwKernelLibraryVersionAddr` and `dwXAPILibraryVersionAddr` in the XBE header.

## Edge Cases and Gotchas

### The dsound debug assert black screen

After getting the basic build working, we hit a persistent black screen that turned out to be `dsoundd.lib` (the debug DirectSound library) asserting internally in `mcpxcore.cpp`. Without a debugger attached, `RtlAssert` just halts the thread. The Xbox sits there with a black screen, looking exactly like a crash.

**Fix**: Use `dsound.lib` (release) instead of `dsoundd.lib`. Same for `d3d8.lib` vs `d3d8d.lib` -- the debug libs assert on things that aren't actually errors, they're validation checks that assume a debugger is present.

This one cost us a few hours because every build iteration looked like "still broken" when it was actually "working fine but paused on an assert nobody can see."

### The 64KB stack

An early iteration of the Makefile had `/stack:65536` (64KB). The working MSVC build used `/stack:1048576` (1MB). The Xbox kernel and XDK runtime expect a reasonably large stack for initialization. With 64KB, the boot process stack-overflows before D3D even initializes.

Symptom: black screen. No crash handler fires because the stack is already gone.

### The Locale.h / locale.h case sensitivity collision

macOS APFS is case-insensitive by default. The Theseus source tree has `Locale.h` (the Xbox locale node header). The MSVC 7.1 STL has `#include <locale.h>` (the C standard locale header). On macOS, `<locale.h>` finds `Locale.h` instead. The STL then fails because the Xbox locale node header doesn't define `lconv` or `LC_COLLATE`.

**Fix**: Use `-iquote` instead of `-I` for the source directory. Angle-bracket includes (`<locale.h>`) skip `-iquote` paths and find the real CRT header. Quoted includes (`"Locale.h"`) still find the source file.

This one was particularly annoying because the error messages are deep in the STL template instantiation chain and don't mention `locale.h` at all. You see `unknown type name 'lconv'` in `xlocinfo` and spend 20 minutes wondering what `xlocinfo` even is.

### The MSVC 7.1 STL and clang

The XDK ships MSVC 7.1's STL implementation. It's old. It has:

- Missing `typename` keywords in dependent contexts (required by the C++ standard, ignored by MSVC)
- `try`/`catch` blocks in `<istream>` and `<xstring>` (breaks with `-fno-exceptions`)
- Debug heap allocator templates that instantiate differently than MSVC expects

We patched `xstring` to add the missing `typename` and dropped `-fno-exceptions` from the build. The STL headers need exception syntax even though Xbox code never actually throws.

### The CRT include path

The XDK's CRT headers (`locale.h`, `stdio.h`, etc.) live in a `crt/` subdirectory under the SDK include path. The STL headers expect them to be directly findable. Adding `-I$(SDK_INC)/crt` to the include path fixes it, but it has to come after the source includes to avoid shadowing.

## What We Learned

1. **Calling conventions are invisible until they're catastrophic.** The stdcall/cdecl mismatch produces symptoms that look like memory corruption, random crashes, or "the Xbox just doesn't like this binary." There's no error message. It just breaks.

2. **Debug libraries assume debuggers exist.** The Xbox debug libs (`dsoundd.lib`, `d3d8d.lib`) fire `RtlAssert` which halts the thread. Without XBDM attached, this looks identical to a crash or hang.

3. **Case sensitivity matters on case-insensitive filesystems.** When your source file and a system header share a name (modulo case), the include path order determines which one wins. `-iquote` is the clean fix.

4. **The XDK header chain has a specific order.** NT kernel types must be defined before `xtl.h` is processed. The force-include shim (`xdk_compat.h`) exists entirely to enforce this order.

5. **Test one variable at a time.** We made the cxbe section flag fix and the stdcall fix in the same session and never isolated which one actually mattered. Don't do that.

## Timeline

All times US Eastern. This whole thing took about a day and a half.

- **2026-04-05 ~12:00** -- "What if we just... used clang?" First compile attempt. Wall of XDK header errors.
- **2026-04-05 ~13:30** -- `xdk_compat.h` created. Headers compile. Linker explodes on kernel imports.
- **2026-04-05 ~14:15** -- `/alternatename` mappings added. First clean link. cxbe produces XBE.
- **2026-04-05 ~14:45** -- First boot attempt. Black screen. Begin staring at hex dumps.
- **2026-04-05 ~17:00** -- Identified stdcall/cdecl mismatch via Ghidra stack analysis. Added `-Xclang -fdefault-calling-conv=stdcall`.
- **2026-04-05 ~17:20** -- First successful boot on real Xbox hardware. Dashboard loads, UI renders, scripts run.
- **2026-04-05 ~18:00** -- cxbe patched (section flags, library versions). OXDK repo created.
- **2026-04-05 ~22:00** -- Dolphin demo working. README written. Pushed v1.
