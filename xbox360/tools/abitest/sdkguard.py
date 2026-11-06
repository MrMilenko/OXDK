#!/usr/bin/env python3
"""Refuse calls across the SDK boundary that xenon-abi still passes differently.

xenon-abi now matches Microsoft's compiler for scalars, 64 bit values, and
aggregates of up to eight bytes (see llvm/README.md). Two shapes are still
SVR4 shaped:

  - an aggregate argument over eight bytes: we pass a pointer to a copy,
    Microsoft's code expects the bytes themselves in the argument slots
  - a function of ours with such a parameter, whose address is taken: the
    SDK may call it

This compiles each unit to LLVM IR with its own command line, and lists every
call to a function the unit does not define (the SDK, the kernel, the CRT) that
passes an aggregate by value, and every address-taken function of the unit's
own that takes one. Exit status 1 when it finds any.

    sdkguard.py -- clang <the unit's usual flags> -c x360_gl.c -o x360_gl.o
    sdkguard.py --ir build/x360_gl.ll ...       # IR you already have

Only the -c/-o part of the command is replaced; everything else is kept, so the
check sees exactly what the build compiles.
"""
import argparse, re, subprocess, sys

DECL = re.compile(r'^declare\s+.*?@("?[\w.$?@]+"?)\((.*)\)')
DEFN = re.compile(r'^define\s+.*?@("?[\w.$?@]+"?)\((.*)\)')
CALL = re.compile(r'call\s+[^@]*@("?[\w.$?@]+"?)\(')


def scan(ir, unit):
    declared, defined, problems = {}, {}, []
    calls, referenced = set(), set()
    for line in ir.splitlines():
        m = DECL.match(line)
        if m:
            declared[m.group(1)] = m.group(2)
            continue
        m = DEFN.match(line)
        if m:
            defined[m.group(1)] = m.group(2)
            continue
        called = {c.group(1) for c in CALL.finditer(line)}
        calls |= called
        for r in re.finditer(r'@("?[\w.$?@]+"?)', line):
            if r.group(1) not in called or line.count('@' + r.group(1)) > 1:
                referenced.add(r.group(1))
    for name, params in sorted(declared.items()):
        if name in calls and 'byval' in params:
            problems.append(f"{unit}: calls {name} with an aggregate over 8 bytes by value")
    for name, params in sorted(defined.items()):
        if name in referenced and 'byval' in params:
            problems.append(f"{unit}: {name} takes an aggregate over 8 bytes by value and "
                            f"its address is taken (an SDK callback would pass it differently)")
    return problems


def ir_of(cmd):
    out, unit, skip = [], None, False
    for i, a in enumerate(cmd):
        if skip:
            skip = False
            continue
        if a == '-o':
            skip = True
            continue
        if a in ('-c', '-MMD', '-MD'):
            continue
        if a == '-MF':
            skip = True
            continue
        if re.search(r'\.(c|cc|cpp|cxx)$', a):
            unit = a
        out.append(a)
    r = subprocess.run(out + ['-S', '-emit-llvm', '-o', '-'], capture_output=True, text=True)
    if r.returncode != 0:
        raise SystemExit(r.stderr.strip())
    return r.stdout, unit or '?'


def main():
    argv = sys.argv[1:]
    cmd = []
    if '--' in argv:
        i = argv.index('--')
        argv, cmd = argv[:i], argv[i + 1:]
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0], epilog=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--ir', nargs='*', default=[])
    a = ap.parse_args(argv)
    problems = []
    for path in a.ir:
        problems += scan(open(path).read(), path)
    if cmd:
        ir, unit = ir_of(cmd)
        problems += scan(ir, unit)
    for p in problems:
        print("sdkguard: " + p)
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
