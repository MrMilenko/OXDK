#!/usr/bin/env python3
"""Locate the LLVM tools and the XDK, without assuming one machine's layout.

Every path can be overridden from the environment, which is what a build system should
set. Failing that, look in the places LLVM is normally installed on macOS and Linux, then
fall back to PATH. LLVM 22 or newer is required.
"""
import os
import shutil

# The patched clang, which is the only compiler that can build for this target.
# See llvm/README.md. OXDK360_LLVM overrides it.
XENON_LLVM_SEARCH = [
    '~/llvm-xenon/build',
    '~/oxdk360-llvm/build',
    '/opt/oxdk360-llvm',
]

# Where the LLVM utilities may be found if the patched build did not include
# them. They are not target specific, so any LLVM 22 or newer will do.
LLVM_SEARCH = [
    '/opt/homebrew/opt/llvm',      # Homebrew, Apple silicon
    '/usr/local/opt/llvm',         # Homebrew, Intel
    '/usr/lib/llvm-23',
    '/usr/lib/llvm-22',            # Debian and Ubuntu
    '/usr/local/llvm',
]

XDK_SEARCH = [
    '~/xdk360/XDK',
    '~/xdk360-extract/sdk/XDK',
    '/opt/xdk360/XDK',
]


def xenon_llvm():
    """The patched LLVM build, or None if it was not found."""
    env = os.environ.get('OXDK360_LLVM')
    if env:
        return os.path.expanduser(env)
    for p in XENON_LLVM_SEARCH:
        p = os.path.expanduser(p)
        if os.path.isfile(os.path.join(p, 'bin', 'clang')):
            return p
    return None


def llvm_prefix():
    """The LLVM installation to use, or None to mean "whatever is on PATH"."""
    env = os.environ.get('LLVM_PREFIX')
    if env:
        return env
    for p in LLVM_SEARCH:
        if os.path.isfile(os.path.join(p, 'bin', 'clang')):
            return p
    return None


def tool(name, env_var=None):
    """Full path to an LLVM tool. Raises with an actionable message if missing.

    The patched build is searched first so its clang wins, then any other LLVM,
    then PATH. Tools are looked up one at a time because a patched build
    configured for clang alone still has a usable clang.
    """
    if env_var and os.environ.get(env_var):
        return os.environ[env_var]
    for prefix in (xenon_llvm(), llvm_prefix()):
        if not prefix:
            continue
        p = os.path.join(prefix, 'bin', name)
        if os.path.isfile(p):
            return p
    found = shutil.which(name)
    if found:
        return found
    if name == 'clang':
        raise SystemExit(
            "clang not found. The Xbox 360 needs the patched clang; see llvm/README.md "
            "for how to build it, then set OXDK360_LLVM to the build directory.")
    raise SystemExit(
        f"{name} not found. Build it alongside the patched clang, or install LLVM 22 or "
        f"newer and either put it on PATH or set LLVM_PREFIX to its install directory.")


def xdk_dir(explicit=None):
    """The XDK to build against. Explicit argument wins, then XDK_DIR, then a search."""
    for cand in (explicit, os.environ.get('XDK_DIR')):
        if cand:
            cand = os.path.expanduser(cand)
            if not os.path.isfile(os.path.join(cand, 'lib', 'xbox', 'xboxkrnl.lib')):
                raise SystemExit(
                    f"{cand} does not look like an XDK: lib/xbox/xboxkrnl.lib is missing")
            return cand
    for p in XDK_SEARCH:
        p = os.path.expanduser(p)
        if os.path.isfile(os.path.join(p, 'lib', 'xbox', 'xboxkrnl.lib')):
            return p
    raise SystemExit(
        "No XDK found. Set XDK_DIR to your extracted Xbox 360 XDK; see the README for "
        "how to extract one on a Unix host.")


def cache_dir():
    """Where translated XDK archives live. Respects XDG on Linux."""
    env = os.environ.get('OXDK360_CACHE')
    if env:
        return os.path.expanduser(env)
    base = os.environ.get('XDG_CACHE_HOME') or os.path.expanduser('~/.cache')
    return os.path.join(base, 'oxdk360')


if __name__ == '__main__':
    print(f"  OXDK360_LLVM: {xenon_llvm() or '(not found, see llvm/README.md)'}")
    print(f"  LLVM_PREFIX : {llvm_prefix() or '(using PATH)'}")
    for t in ('clang', 'ld.lld', 'llvm-ar', 'llvm-nm', 'llvm-objcopy'):
        try:
            print(f"  {t:12s}: {tool(t)}")
        except SystemExit as e:
            print(f"  {t:12s}: {e}")
    try:
        print(f"  XDK_DIR     : {xdk_dir()}")
    except SystemExit as e:
        print(f"  XDK_DIR     : {e}")
    print(f"  cache       : {cache_dir()}")
