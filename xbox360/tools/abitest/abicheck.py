#!/usr/bin/env python3
"""Check a compiler's PowerPC code against the Xenon ABI, and say pass or fail.

The rules are measured, not documented. The frame layout came from disassembling
Microsoft's own libcMT.lib; the parameter slot size and justification came from the
XDK's vadefs.h, which spells the rule out directly:

    #define _VA_ALIGN(t)             8
    #define _VA_IS_LEFT_JUSTIFIED(t) (sizeof(t) > _VA_ALIGN(t) || \
                                      0 != (sizeof(t) & (sizeof(t)-1)))

so every argument occupies an 8-byte slot, and a scalar sits at the END of its slot,
which is what a callee spilling a 64-bit register with std leaves on a big-endian
machine. What that adds up to:

  linkage area          16 bytes at the bottom of the caller's frame
  argument registers    r3-r10, with a reserved 8-byte slot each above the linkage
                        area, so the first stack argument is at caller_sp+80
  parameter save area   8-byte slots, scalars right justified (an int at slot+4)
  return address        callee spills LR as a WORD at caller_sp-8, before it
                        allocates, rather than into the caller's linkage area
  callee-saved GPRs     stored as DOUBLEWORDS below the caller's sp, via __savegprlr_N

clang's SysV PPC32 puts LR at caller_sp+4 with an 8-byte linkage area and 4-byte
parameter slots starting at +8. AIX puts LR at +8 with a 24-byte linkage area. Neither
matches, which is why this needs a backend patch rather than a target triple.

    abicheck.py                                     # the default clang
    abicheck.py --cc /path/to/clang --feature xenon-abi
"""
import argparse, re, subprocess, sys, os, tempfile

SOURCE = r'''
extern int g(int,int,int,int,int,int,int,int,int,int,int,int);

/* Twelve integers: eight land in r3-r10, the rest go through the parameter save
   area, so the same source exercises both sides of the convention. */
int call12(void) { return g(1,2,3,4,5,6,7,8,9,10,11,12); }

int recv12(int a,int b,int c,int d,int e,int f,int h,int i,
           int j,int k,int l,int m) { return j + k + l + m; }
'''

# What a Xenon compiler must produce for arguments 9 through 12: an 8-byte slot each
# starting at caller_sp+80, with the int right justified inside it.
WANT = [84, 92, 100, 108]


def compile_asm(cc, extra):
    with tempfile.TemporaryDirectory() as d:
        src = os.path.join(d, 't.c')
        open(src, 'w').write(SOURCE)
        cmd = [cc, '-target', 'powerpc-unknown-none-elf', '-O1', '-S', '-o', '-', src] + extra
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode != 0:
            raise SystemExit(r.stderr.strip() or 'compile failed')
        return r.stdout


def body(asm, name):
    """The assembly of one function."""
    m = re.search(rf'^{name}:.*?^\.Lfunc_end', asm, re.M | re.S)
    return m.group(0) if m else ''


def lr_slot(fn):
    """Where the callee spills LR, relative to the caller's stack pointer."""
    m = re.search(r'mflr\s+(\d+)', fn)
    if not m:
        return None
    reg = m.group(1)
    alloc = re.search(r'stwu\s+1,\s*-(\d+)\(1\)', fn)
    spill = re.search(rf'stw\s+{reg},\s*(-?\d+)\(1\)', fn)
    if not spill:
        return None
    off = int(spill.group(1))
    # A spill before the stwu is already caller relative; one after it is not.
    if alloc and spill.start() > alloc.start():
        off -= int(alloc.group(1))
    return off


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--cc', default=None, help='compiler to test')
    ap.add_argument('--abi', default=None, help='pass -mabi=<abi>')
    ap.add_argument('--feature', default=None, help='enable a subtarget feature, eg xenon-abi')
    ap.add_argument('--show', action='store_true', help='print the assembly too')
    a = ap.parse_args()

    cc = a.cc
    if not cc:
        sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'common'))
        import toolchain
        cc = toolchain.tool('clang')

    extra = [f'-mabi={a.abi}'] if a.abi else []
    if a.feature:
        extra += ['-Xclang', '-target-feature', '-Xclang', f'+{a.feature}']

    asm = compile_asm(cc, extra)
    if a.show:
        print(asm)

    caller, callee = body(asm, 'call12'), body(asm, 'recv12')
    stores = sorted({int(m.group(1)) for m in re.finditer(r'stw\s+\d+,\s*(\d+)\(1\)', caller)})
    loads = sorted({int(m.group(1)) for m in re.finditer(r'lwz\s+\d+,\s*(\d+)\(1\)', callee)})
    stores = [o for o in stores if o >= 16]
    lr = lr_slot(caller)

    label = cc + (f"  -mabi={a.abi}" if a.abi else "") + (f"  +{a.feature}" if a.feature else "")
    print(f"compiler: {label}\n")

    checks = [
        ("LR spilled at caller_sp-8", lr == -8,
         f"at caller_sp{lr:+d}" if lr is not None else "not spilled",
         {4: "SysV PPC32", 8: "AIX / PowerOpen"}.get(lr, "")),
        ("stack arguments 9-12 stored at +80 in 8-byte slots", stores == WANT,
         str(stores), ""),
        ("callee reads them from the same offsets", loads == WANT,
         str(loads), ""),
    ]
    ok = True
    for name, passed, got, note in checks:
        ok &= passed
        mark = "ok  " if passed else "FAIL"
        extra_note = f"   [{note}]" if note and not passed else ""
        print(f"  {mark}  {name}")
        print(f"        got {got}, want {'caller_sp-8' if 'LR' in name else WANT}{extra_note}")

    print()
    print("Xenon ABI: " + ("all checks passed" if ok else "NOT satisfied by this compiler"))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
