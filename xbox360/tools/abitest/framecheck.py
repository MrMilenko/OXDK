#!/usr/bin/env python3
"""Check compiled objects for Xenon frame layout violations, offline.

The Xenon return address lives at caller_sp-8, inside the callee's OWN frame rather than
in the caller's linkage area. That makes two overlaps possible which no other PowerPC ABI
can produce, and both of them corrupt the return address:

  1. a callee-saved register or a local assigned the same slot as LR
  2. a frame small enough that the LR slot falls inside the function's own outgoing
     parameter save area, which spans caller_sp+16 to caller_sp+80, where any callee
     spilling its argument registers will overwrite it

The second hangs the console rather than faulting. Both are cheap to find in
the object file, so there is no reason to find them on hardware.

    framecheck.py main.o [more.o ...]
"""
import re, subprocess, sys, os

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'common'))
import toolchain

FUNC = re.compile(r'^([0-9a-f]+)\s+<(.+)>:')
INSN = re.compile(r'^\s+([0-9a-f]+):\s+(?:[0-9a-f]{2} ){4}\s*(\S+)\s*(.*)$')

# The parameter save area a callee will spill its argument registers into.
PARAM_LO, PARAM_HI = 16, 80


def objdump(path):
    # Same search as the rest of the toolchain, rather than one machine's paths.
    try:
        candidates = [toolchain.tool('llvm-objdump')]
    except SystemExit:
        candidates = []
    candidates += ['llvm-objdump', 'objdump']
    for tool in candidates:
        try:
            r = subprocess.run([tool, '-d', path], capture_output=True, text=True)
            if r.returncode == 0:
                return r.stdout
        except FileNotFoundError:
            continue
    raise SystemExit("no objdump found")


def functions(text):
    name, insns = None, []
    for line in text.splitlines():
        m = FUNC.match(line)
        if m:
            if name:
                yield name, insns
            name, insns = m.group(2), []
            continue
        m = INSN.match(line)
        if m and name:
            insns.append((m.group(2), m.group(3).strip()))
    if name:
        yield name, insns


def check(path):
    problems = []
    for name, insns in functions(objdump(path)):
        frame = lr_off = None
        stores = []       # (offset, register, mnemonic)
        calls = False
        for mn, ops in insns:
            if mn == 'stwu':
                m = re.match(r'1,\s*(-?\d+)\(1\)', ops)
                if m and frame is None:
                    frame = -int(m.group(1))
            elif mn in ('stw', 'std'):
                m = re.match(r'(\d+),\s*(-?\d+)\(1\)', ops)
                if m:
                    reg, off = int(m.group(1)), int(m.group(2))
                    if off < 0 and reg == 0:
                        lr_off = off          # the LR spill, before the frame exists
                    else:
                        stores.append((off, reg, mn))
            elif mn == 'bl':
                calls = True

        if frame is None or lr_off is None:
            continue
        slot = frame + lr_off                 # LR's address relative to the new sp

        for off, reg, mn in stores:
            width = 8 if mn == 'std' else 4
            if off < slot + 4 and off + width > slot:
                problems.append(f"{path}: {name}: r{reg} stored at +{off} overlaps the "
                                f"return address at +{slot} (frame {frame})")

        if calls and slot < PARAM_HI:
            problems.append(f"{path}: {name}: return address at +{slot} lies inside this "
                            f"function's own parameter save area (+{PARAM_LO}..+{PARAM_HI}); "
                            f"a callee spilling its argument registers will overwrite it "
                            f"(frame {frame})")
    return problems


def main(argv):
    if not argv:
        print(__doc__)
        return 2
    bad = []
    for p in argv:
        bad += check(p)
    for b in bad:
        print("  FAIL  " + b)
    n = len(argv)
    print(f"framecheck: {n} object{'s' if n != 1 else ''}, "
          + (f"{len(bad)} violation{'s' if len(bad) != 1 else ''}" if bad else "no violations"))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
