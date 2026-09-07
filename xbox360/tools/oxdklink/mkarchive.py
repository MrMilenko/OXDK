#!/usr/bin/env python3
"""Translate a Xenon COFF static library into an ELF archive ld.lld can link against.

Members are split out by arsplit rather than llvm-ar: llvm-ar silently drops some XDK
members whose names are long Windows paths, which quietly cost us definitions like
RtlAllocateHeap and made a link look as though the XDK were missing them.

Short import records are not code and are returned separately, to be turned into XEX
import thunks rather than linked.
"""
import os, subprocess, sys, hashlib, json

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import arsplit

COFF2ELF = os.path.join(HERE, '..', 'coff2elf', 'coff2elf.py')
sys.path.insert(0, os.path.join(HERE, '..', 'common'))
import toolchain

AR = toolchain.tool('llvm-ar', 'LLVM_AR')


def build(lib, outdir, quiet=True):
    """Return (archive path, imports) where imports is [(name, ordinal, type, dll)]."""
    name = os.path.basename(lib).replace('.lib', '')
    work = os.path.join(outdir, name)
    archive = os.path.join(outdir, name + '.a')
    impfile = os.path.join(outdir, name + '.imports.json')
    stamp = os.path.join(work, 'stamp')
    aliasfile = os.path.join(work, 'aliases.json')
    # The cached objects are only valid for the translator that produced them, so the
    # key covers coff2elf's own source as well as the library. Keyed on the library
    # alone, a fix to the translator leaves every already translated archive in
    # place and silently keeps using it, which is how a corrected weak external or
    # relocation can appear to change nothing at all.
    translator = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                              '..', 'coff2elf', 'coff2elf.py')
    try:
        with open(translator, 'rb') as f:
            tkey = hashlib.sha1(f.read()).hexdigest()
    except OSError:
        tkey = 'unknown'
    key = hashlib.sha1(
        f"{os.path.getsize(lib)}:{os.path.getmtime(lib)}:{tkey}".encode()).hexdigest()
    if (os.path.exists(stamp) and open(stamp).read() == key
            and os.path.exists(archive) and os.path.exists(impfile)
            and os.path.exists(aliasfile)):
        return (archive, [tuple(x) for x in json.load(open(impfile))],
                [tuple(x) for x in json.load(open(aliasfile))])

    objs, imports, aliases = arsplit.split(lib, os.path.join(work, 'coff'))
    elfdir = os.path.join(work, 'elf')
    os.makedirs(elfdir, exist_ok=True)
    out, failed = [], []
    for o in objs:
        dst = os.path.join(elfdir, os.path.basename(o) + '.o')
        r = subprocess.run([sys.executable, COFF2ELF, o, '-o', dst, '-q'],
                           capture_output=True, text=True)
        if r.returncode == 0:
            out.append(dst)
        else:
            failed.append((os.path.basename(o), r.stderr.strip().split('\n')[-1][:60]))

    if os.path.exists(archive):
        os.remove(archive)
    for i in range(0, len(out), 200):
        subprocess.run([AR, 'rcs', archive] + out[i:i + 200], check=True, capture_output=True)
    json.dump(imports, open(impfile, 'w'))
    json.dump(aliases, open(aliasfile, 'w'))
    open(stamp, 'w').write(key)
    if not quiet:
        print(f"  {name}: {len(out)} objects, {len(imports)} imports, {len(failed)} failed")
        for f in failed[:4]:
            print(f"      {f[0]}: {f[1]}")
    elif failed:
        # Never swallow these. A member the translator refuses is code the link will
        # not have, and the link does not fail: it resolves whatever referenced it to
        # something else or leaves it weak and undefined, which reads as a clean
        # build. That is how 222 vtable slots once came out null.
        print(f"mkarchive: {name}: {len(failed)} of {len(objs)} members could not be "
              f"translated and are missing from the link", file=sys.stderr)
        for f in failed[:4]:
            print(f"    {f[0]}: {f[1]}", file=sys.stderr)
        if len(failed) > 4:
            print(f"    ... and {len(failed) - 4} more (use --verbose for the rest)",
                  file=sys.stderr)
    return archive, imports, aliases


if __name__ == '__main__':
    for lib in sys.argv[2:]:
        build(lib, sys.argv[1], quiet=False)
