# Working on OXDK

Repository internals and the things that go wrong. To build a project, read
[xbox.md](xbox.md) or [xbox360.md](xbox360.md) instead.

## Layout

```
oxdk.mk             the front door, dispatches on OXDK_TARGET
common/toolchain.mk finds clang, lld, the utilities, libc++ and the XDKs
common/libcxx/      libc++ headers CMake would generate, shared
scripts/            build-llvm.sh, doctor.sh, oxdk-env.sh
oxdk                launcher, opens a shell with the environment set
xbox/xbox.mk        the original Xbox
xbox360/xbox360.mk  the Xbox 360
```

Both consoles are included by `oxdk.mk` after `common/toolchain.mk`, so
discovery happens once. `scripts/oxdk-env.sh` asks the same makefile for its
values, so a shell and a build never disagree.

Nothing hardcodes a path. Anything in the environment is used as given, and
anything else is searched for. `./oxdk doctor` prints what was found.

## Adding a tool

Put it under the console it belongs to, and add its directory to the PATH list
in `scripts/oxdk-env.sh` if it is run by hand rather than by make.

## Kernel import decoration, original Xbox

`xboxkrnl.lib` exports stdcall decorated names (`_HalReturnToFirmware@4`).
clang emits undecorated imports (`__imp__HalReturnToFirmware`) and lld-link
cannot match them. `xbox/xbox.mk` carries `/alternatename` mappings for the
common kernel functions. For one that is not mapped:

```
/alternatename:__imp__YourFunction=__imp__YourFunction@N
```

`N` is the parameter size in bytes.

## Floating point support, Xbox 360

`printf("%f")` reporting `floating point support not loaded` and bugchecking
means `_fltused` was not in the link. `oxdklink` forces it. Anything else the
CRT pulls in by symbol reference rather than by call needs the same treatment,
via `CRT_FORCED` in `xbox360/tools/oxdklink/oxdklink.py`.

## Black screens are usually not crashes

Debug libraries assert. `dsoundd.lib` and `d3d8d.lib` call `RtlAssert`, which
halts the thread with no debugger attached and looks like a hang. Link the
release libraries.

The stack may be too small. Use `/stack:1048576`. 64 KB overflows during boot,
before any crash handler exists.

## Case sensitivity

The XDK's filenames are mixed case and its headers include each other with
inconsistent case. On a case sensitive filesystem run
`xbox/tools/normalize-xdk.sh` over the tree once.

## Testing a change

```sh
./oxdk doctor
make -C xbox/samples/libcxx/cxx17_hello
make -C xbox/samples/d3d/hello
make -C xbox360/samples/d3dclear
make -C xbox360/samples/title
make -C xbox360/tools/abitest/console
python3 xbox360/tools/abitest/abicheck.py --cc "$OXDK_LLVM/bin/clang" --feature xenon-abi
```

`framecheck.py` over the resulting objects checks for the frame overlaps the
Xenon ABI makes possible.

## Comment style

One block above the thing it describes, explaining what it does and any
constraint a reader would not guess. Not a history of what it used to do.
