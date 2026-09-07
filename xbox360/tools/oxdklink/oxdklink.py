#!/usr/bin/env python3
"""Link an Xbox 360 title from clang objects and the real XDK libraries.

The XDK's libraries are Xenon COFF, which no LLVM tool will open, so they are translated
to ELF archives first (see mkarchive.py). What is left over after linking is the set of
functions the console provides at load time, the kernel and xam, and those are not
linked at all: the XEX loader patches them in. This driver resolves them by generating,
for each one, a pointer slot and a 16-byte thunk of the shape cxex knows how to patch,
then links again so every call site binds to a thunk.

    pass 1   link, ignoring undefined symbols, to discover what the console must supply
    stubs    emit a slot and a thunk for each, as assembly, and assemble it
    pass 2   link again with the stubs, so nothing is undefined
    manifest read the thunk and slot addresses back out and describe them for cxex

Entry is mainCRTStartup out of xapilib, so a title initialises exactly the way one built
on Windows does: XapiInitProcess sets up the heap and TLS before main is called.
"""
import argparse, json, os, struct, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, '..', 'xexutil'))
import mkarchive, implib

sys.path.insert(0, os.path.join(HERE, '..', 'common'))
import toolchain

CLANG = toolchain.tool('clang')
NM = toolchain.tool('llvm-nm', 'LLVM_NM')
OBJCOPY = toolchain.tool('llvm-objcopy')
LLD = os.environ.get('LLD', 'ld.lld')
TARGET = 'powerpc-unknown-none-elf'

# A few CRT entries are spelled with a leading underscore by the compiler and exported
# without one by the XDK's libraries. oldnames.lib covers the POSIX family (stricmp,
# strdup, unlink) and these are what it does not: setjmp is declared as _setjmp by the
# XDK's own <setjmp.h>, which is what libpng and anything else using it emits a call to,
# while libcMT exports the name unprefixed. The target is forced into the link with -u,
# since nothing references it under the name the archive actually holds.
CRT_ALIASES = {'_setjmp': 'setjmp'}

# MSVC emits a reference to _fltused from every object that touches floating
# point, and that reference is what pulls the CRT's floating point printf into
# the link. clang emits nothing, so printf reaches a stub that prints
# "floating point support not loaded" and calls KeBugCheck. libcMT defines the
# symbol, so forcing it in is enough. Costs a few KB in a title that never
# formats a float, against a crash in one that does.
CRT_FORCED = ['_fltused']

# xam first so it takes library index 0, which is the order imagexex uses.
LIBRARIES = [('xam.xex', '2.0.21256.0', '2.0.1861.0', '0xfca15c76'),
             ('xboxkrnl.exe', '2.0.21256.0', '2.0.1861.0', '0x45dc17e0')]


def undefined(elf):
    """Undefined symbols that must be satisfied.

    Weak undefined symbols are skipped: they are allowed to stay unresolved and resolve to
    zero, which is how C++ deleting destructors and similar optional references appear.
    """
    out = subprocess.run([NM, '-u', elf], capture_output=True, text=True).stdout
    names = set()
    for line in out.splitlines():
        f = line.split()
        if not f:
            continue
        if len(f) >= 2 and f[-2].lower() == 'w':      # weak undefined
            continue
        names.add(f[-1])
    return sorted(names)


def archive_defines(archives, names):
    """Which of these symbols some archive can define, so we can force it to be pulled."""
    have = set()
    for a in archives:
        out = subprocess.run([NM, '--defined-only', a], capture_output=True, text=True).stdout
        for line in out.splitlines():
            f = line.split()
            if len(f) >= 3 and f[2] in names:
                have.add(f[2])
    return have


def cxx_main_alias(objs):
    """The mangled name of a C++ main, if the objects define one.

    -ffreestanding is what keeps the XDK's own C++ library out of reach: its headers sit
    in the same directory as the C ones, so only the freestanding path stops libc++ from
    resolving back into Microsoft's. The cost is that main stops being special: in a
    freestanding environment the entry point is implementation defined, so a C++ main is
    an ordinary function and carries a mangled name. The XDK's mainCRTStartup calls plain
    main, so the two never meet and the title links with main undefined. Aliasing here
    means a port keeps the main it was written with.
    """
    out = subprocess.run([NM, '--defined-only'] + list(objs),
                         capture_output=True, text=True).stdout
    found = set()
    for line in out.splitlines():
        f = line.split()
        if f and f[-1].startswith('_Z4main'):
            found.add(f[-1])
    if len(found) > 1:
        raise SystemExit("oxdklink: more than one C++ main is defined: "
                         + ", ".join(sorted(found)))
    return found.pop() if found else None


def resolve_cxx_main(needed, objs, aliases):
    """Bind an undefined main to the C++ definition, if that is what is going on."""
    if 'main' not in needed:
        return
    alias = cxx_main_alias(objs)
    if alias:
        needed.remove('main')
        aliases.append(('main', alias))


def symbols(elf):
    out = subprocess.run([NM, elf], capture_output=True, text=True).stdout
    return {p[2]: int(p[0], 16) for p in (l.split() for l in out.splitlines()) if len(p) == 3}


def link(objs, archives, script, entry, out, allow_undef, keep=(), defsyms=()):
    cmd = [LLD, '-T', script, '-e', entry, '-u', entry, '-o', out]
    for k in keep:
        cmd += ['-u', k]
    # The librarian's alias members are machine 0x0000 and cannot be translated, so an
    # alias like vsnprintf -> _vsnprintf is applied here instead of being lost.
    for alias, target in defsyms:
        cmd.append(f'--defsym={alias}={target}')
    cmd += objs + archives
    if allow_undef:
        cmd.append('--unresolved-symbols=ignore-all')
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        sys.stderr.write(r.stderr)
        raise SystemExit(f"link failed: {out}")
    return out


def write_stubs(path, imports):
    """A pointer slot per import, plus a 16-byte thunk for the code ones.

    cxex fills the slot with (library_index << 16) | ordinal and the thunk with two tagged
    placeholder words followed by mtctr/bctr, which the loader rewrites into the resolved
    address.

    Deliberately no ABI shim here. A shim inserts a frame between caller and callee, and a
    Xenon function taking more than eight integer arguments reads the rest out of the
    CALLER's parameter save area, and NtCreateFile takes eleven. Shimming every import
    silently corrupts those calls for XDK code, which is already ABI-correct and needs no
    help. The shim belongs at our own call sites instead, where we know clang generated
    the caller; see the samples.
    """
    with open(path, 'w') as f:
        f.write("// generated by oxdklink.py: import slots and thunks\n")
        f.write('    .section .rdata, "a"\n')
        for name, _, _, typ in imports:
            f.write(f"    .globl  __imp_slot_{name}\n__imp_slot_{name}:\n    .space 4\n")
        f.write('\n    .section .text, "ax"\n    .align 4\n')
        for name, _, _, typ in imports:
            if typ == 0:
                f.write(f"    .globl  {name}\n{name}:\n    .space 16\n")
        for name, _, _, typ in imports:
            if typ != 0:
                f.write(f"    .globl  {name}\n    .set    {name}, __imp_slot_{name}\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('objects', nargs='+', help='clang-compiled ELF objects')
    ap.add_argument('-o', '--output', required=True, help='output XEX')
    ap.add_argument('-T', '--script', required=True, help='linker script')
    ap.add_argument('--xdk', default=None,
                    help='extracted XDK; defaults to $XDK_DIR or a short search')
    ap.add_argument('--libs', default='xapilib,libcMT')
    ap.add_argument('--cache', default=None,
                    help='where translated XDK archives are kept')
    ap.add_argument('--entry', default='mainCRTStartup')
    ap.add_argument('--keep', action='append', default=[],
                    help='force this symbol to be pulled in even if unreferenced')
    ap.add_argument('--base', default='0x82000000')
    ap.add_argument('--stack', default='0x40000')
    ap.add_argument('--name', default='title.exe')
    ap.add_argument('--title-id', default='0x0',
                    help="the title's id, as declared in its xex.xml. Anything the console "
                         "keys by title: XContent save packages, profiles, achievements. "
                         "resolves through this, so a title that saves needs a real one")
    ap.add_argument('-v', '--verbose', action='store_true')
    a = ap.parse_args()

    a.xdk = toolchain.xdk_dir(a.xdk)
    a.cache = a.cache or toolchain.cache_dir()

    work = os.path.join(os.path.dirname(os.path.abspath(a.output)), '.oxdklink')
    os.makedirs(work, exist_ok=True)

    archives, xam_imports, lib_aliases = [], [], {}
    for lib in a.libs.split(','):
        path = os.path.join(a.xdk, 'lib', 'xbox', lib + '.lib')
        arc, imps, als = mkarchive.build(path, a.cache, quiet=not a.verbose)
        archives.append(arc)
        xam_imports += imps
        lib_aliases.update(als)

    krnl = implib.read(os.path.join(a.xdk, 'lib', 'xbox', 'xboxkrnl.lib'))
    krnl_by_name = {v[0]: (o, v[1]) for o, v in krnl.items()}
    # Short import records carry the module they come from. Keyed without it, every
    # library's imports were attributed to LIBRARIES[0], so adding xbdm to XDK_LIBS
    # bound each Dm call to whatever xam export shared its ordinal.
    imp_by_name = {r[0]: (r[1], r[2], r[3]) for r in xam_imports}
    lib_index = {name: i for i, (name, *_rest) in enumerate(LIBRARIES)}

    # pass 1: what does the console have to supply?
    stage1 = link(a.objects, archives, a.script, a.entry,
                  os.path.join(work, 'stage1.elf'), allow_undef=True, keep=a.keep)
    needed = undefined(stage1)

    imports = []
    unknown = []
    resolved_aliases = []
    forced = []
    resolve_cxx_main(needed, a.objects, resolved_aliases)
    for n in needed:
        if n in imp_by_name:
            ordinal, typ, dll = imp_by_name[n]
            mod = dll.split('@')[0]
            if mod not in lib_index:
                raise SystemExit(
                    f"oxdklink: {n} is imported from {mod}, whose library id and "
                    f"version this tool does not know. Binding it to "
                    f"{LIBRARIES[0][0]} would silently call an unrelated export, so "
                    f"drop that library from --libs until {mod} is added to LIBRARIES.")
            imports.append((n, lib_index[mod], ordinal, typ))
        elif n in krnl_by_name:
            imports.append((n, 1, krnl_by_name[n][0], krnl_by_name[n][1]))
        elif n in lib_aliases:
            resolved_aliases.append((n, lib_aliases[n]))
        elif n in CRT_ALIASES:
            resolved_aliases.append((n, CRT_ALIASES[n]))
            forced.append(CRT_ALIASES[n])
        else:
            unknown.append(n)
    if unknown:
        # Some of these are real definitions sitting in an archive member nothing has
        # pulled yet, typically a weak COMDAT copy in our own object that lost to a
        # library copy. Force those members in and link again rather than giving up.
        pullable = archive_defines(archives, set(unknown))
        if pullable:
            if a.verbose:
                print(f"  forcing {len(pullable)} archive definitions: "
                      + ', '.join(sorted(pullable)[:4]))
            stage1 = link(a.objects, archives, a.script, a.entry,
                          os.path.join(work, 'stage1.elf'), allow_undef=True,
                          keep=list(a.keep) + sorted(pullable))
            a.keep = list(a.keep) + sorted(pullable)
            needed = undefined(stage1)
            imports, unknown = [], []
            resolve_cxx_main(needed, a.objects, resolved_aliases)
            for n in needed:
                if n in imp_by_name:
                    ordinal, typ, dll = imp_by_name[n]
                    mod = dll.split('@')[0]
                    if mod not in lib_index:
                        raise SystemExit(
                            f"oxdklink: {n} is imported from {mod}, whose library id "
                            f"and version this tool does not know.")
                    imports.append((n, lib_index[mod], ordinal, typ))
                elif n in krnl_by_name:
                    imports.append((n, 1, krnl_by_name[n][0], krnl_by_name[n][1]))
                else:
                    unknown.append(n)
        if unknown:
            raise SystemExit("oxdklink: not provided by the console and not in any library:\n  "
                             + "\n  ".join(unknown))

    # Reserve the header region. cxex writes the PE headers into the first 0x400 bytes of
    # the image, so something has to occupy that range or objcopy starts the binary at the
    # first real section and every RVA comes out 0x400 low.
    pehdr_s = os.path.join(work, 'pehdr.S')
    with open(pehdr_s, 'w') as f:
        f.write('    .section .pehdr, "a"\n    .space 0x400\n')
    pehdr_o = os.path.join(work, 'pehdr.o')
    subprocess.run([CLANG, '-target', TARGET, '-c', pehdr_s, '-o', pehdr_o], check=True)

    stubs = os.path.join(work, 'imports.S')
    write_stubs(stubs, [(n, lib, o, t) for n, lib, o, t in imports])
    stub_o = os.path.join(work, 'imports.o')
    subprocess.run([CLANG, '-target', TARGET, '-c', stubs, '-o', stub_o], check=True)

    forced += [n for n in CRT_FORCED if n not in forced]

    if forced:
        a.keep = list(a.keep) + forced

    # pass 2: nothing may be undefined now
    final = link([pehdr_o] + a.objects + [stub_o], archives, a.script, a.entry,
                 os.path.join(work, 'title.elf'), allow_undef=False, keep=a.keep,
                 defsyms=resolved_aliases)

    binimg = os.path.join(work, 'title.bin')
    subprocess.run([OBJCOPY, '-O', 'binary', final, binimg], check=True)
    syms = symbols(final)
    rows = section_rows(final, int(a.base, 0), 0)
    sort_pdata(binimg, rows, a.verbose)
    image_size = os.path.getsize(binimg)

    manifest = os.path.join(work, 'title.mf')
    with open(manifest, 'w') as f:
        f.write("# generated by oxdklink.py\n")
        f.write(f"base {a.base}\nentry 0x{syms[a.entry]:08x}\nstack {a.stack}\n")
        # cxex emits EXECUTION_INFO only for a non-zero title id, and the console keys
        # XContent by it: with 0 a title's save package resolves somewhere it will not
        # find again, and the write still reports success.
        f.write(f"titleid {a.title_id}\nsysflags 0x00000001\npename {a.name}\n\n")
        for row in section_rows(final, int(a.base, 0), image_size):
            f.write(row + "\n")
        for i, (libname, ver, vmin, libid) in enumerate(LIBRARIES):
            rows = [x for x in imports if x[1] == i]
            if not rows:
                continue
            f.write(f"\nlibrary {libname} {ver} {vmin} {libid}\n")
            for name, _, ordinal, typ in rows:
                slot = syms['__imp_slot_' + name]
                if typ == 0:
                    f.write(f"import {ordinal} {slot:x} {syms[name]:x}\n")
                else:
                    f.write(f"import {ordinal} {slot:x}\n")

    cxex = os.path.join(HERE, '..', 'cxex', 'cxex')
    subprocess.run([cxex, '-m', manifest, '-i', binimg, '-o', a.output], check=True)
    ncode = sum(1 for x in imports if x[3] == 0)
    print(f"  {len(a.objects)} objects + {len(archives)} XDK libraries")
    print(f"  {len(imports)} imports ({ncode} code, {len(imports)-ncode} data)")


def sort_pdata(binimg, rows, verbose):
    """Sort the exception table by address.

    A PE exception table has to be sorted by BeginAddress; lookup is a binary search over
    it. MSVC's linker sorts .pdata as it merges the contributions, but ld.lld has no reason
    to know that and simply concatenates them in object order, which leaves the table
    unsorted wherever the link order does not match the code order.
    """
    for row in rows:
        f = row.split()
        if f[1] != '.pdata':
            continue
        rva, size = int(f[2], 16), int(f[3], 16)
        d = bytearray(open(binimg, 'rb').read())
        n = size // 8
        ents = [bytes(d[rva + i*8: rva + i*8 + 8]) for i in range(n)]
        order = sorted(ents, key=lambda e: struct.unpack('>I', e[:4])[0])
        if order != ents:
            d[rva:rva + n*8] = b''.join(order)
            open(binimg, 'wb').write(bytes(d))
            if verbose:
                print(f"  sorted {n} exception table entries")
        return


def section_rows(elf, base, image_size):
    """Describe the linked sections for cxex, taken from the ELF rather than assumed.

    Protection is per 64K page, so each section the loader must treat differently has to
    start on a page boundary; the linker script is what arranges that. Sizes come from the
    link so .text can grow without anyone editing a table.
    """
    out = subprocess.run([toolchain.tool('llvm-objdump'), '-h', elf],
                         capture_output=True, text=True).stdout
    kinds = {'.rdata': 0x40000040, '.rodata': 0x40000040,
             '.pdata': 0x40000040, '.xdata': 0x40000040,
             '.text': 0x60000020,
             '.data': 0xc0000040, '.bss': 0xc0000040}
    rows, seen = [], {}
    for line in out.splitlines():
        f = line.split()
        if len(f) < 4 or not f[0].isdigit():
            continue
        name, size, vma = f[1], int(f[2], 16), int(f[3], 16)
        if name not in kinds or not size:
            continue
        rva = vma - base
        # merge sections that share a name prefix into one row per kind
        key = name
        if key in seen:
            lo, hi, ch = seen[key]
            seen[key] = (min(lo, rva), max(hi, rva + size), ch)
        else:
            seen[key] = (rva, rva + size, kinds[name])
    for name, (lo, hi, ch) in sorted(seen.items(), key=lambda x: x[1][0]):
        rows.append(f"section {name} {lo:x} {hi - lo:x} {ch:08x}")
    return rows


if __name__ == '__main__':
    main()
