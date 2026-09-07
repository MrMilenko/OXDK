// Does the Xenon ABI support in clang agree with Microsoft's compiler?
//
// sprintf lives in libcMT and was built by Microsoft, so it reads its variable
// arguments the way the XDK's own vadefs.h describes: eight byte slots beginning at
// caller_sp+80, with scalars right justified inside their slot. Handing it more
// arguments than fit in r3-r10 makes it walk the parameter save area that our compiler
// laid out. Get the layout right and the formatted string is correct; get it wrong and
// the tail of it is garbage.
//
// There are no shims here. Every call goes straight into XDK code.

// A callee written by hand to the measured convention, in probe.S. It returns one bit
// per argument that arrived where Microsoft's compiler would have put it.
int abi_probe(int a, float b, int c, int d, int e, int f, int g, int h, int i);
#define PROBE_ALL 0x1FF

void DbgPrint(const char *fmt, ...);
void HalReturnToFirmware(unsigned int routine);
int sprintf(char *buf, const char *fmt, ...);

// The compiler's own varargs, rather than the XDK's stdarg.h, because what is being
// checked here is exactly what the compiler generates. Under this ABI __builtin_va_list
// is a char*, the same thing the XDK's vadefs.h declares, so the two agree.
typedef __builtin_va_list va_list;
#define va_start(ap, x) __builtin_va_start(ap, x)
#define va_arg(ap, t)   __builtin_va_arg(ap, t)
#define va_end(ap)      __builtin_va_end(ap)

int vsprintf(char *buf, const char *fmt, va_list ap);

// Quake writes all of its console output through a function shaped like this one.
// It is the case that needs our va_list to have the shape Microsoft's code expects
// and our argument homing to match what wsprintfA does.
static void con_printf(char *out, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsprintf(out, fmt, ap);
    va_end(ap);
}

// And the other direction: our own va_arg walk, reading what our own caller wrote.
static int sum(int n, ...)
{
    va_list ap;
    int t = 0, i;
    va_start(ap, n);
    for (i = 0; i < n; i++)
        t += va_arg(ap, int);
    va_end(ap);
    return t;
}

static int run, failed;

static int streq(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

// The harness never formats anything itself. Every DbgPrint here takes a single
// argument, so the only variadic call that crosses into Microsoft's code is the
// sprintf under test, and a harness bug cannot be mistaken for an ABI bug.
static void check(const char *name, const char *got, const char *want)
{
    int ok = streq(got, want);
    run++;
    if (!ok)
        failed++;
    DbgPrint(ok ? "  ok    " : "  FAIL  ");
    DbgPrint(name);
    DbgPrint("\n          got  [");
    DbgPrint(got);
    if (!ok) {
        DbgPrint("]\n          want [");
        DbgPrint(want);
    }
    DbgPrint("]\n");
}

int main(void)
{
    char buf[256];

    DbgPrint("\nOXDK360: Xenon ABI against Microsoft's own sprintf\n\n");

    // Fourteen arguments. Six of them are past r3-r10 and travel through the
    // parameter save area.
    sprintf(buf, "%d %d %d %d %d %d %d %d %d %d %d %d",
            1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12);
    check("twelve ints, six of them on the stack", buf, "1 2 3 4 5 6 7 8 9 10 11 12");

    // Mixed widths, so a char has to be right justified in its slot the same way an
    // int is.
    sprintf(buf, "%s %d %u %d %d %d %d %d %c", "str", -1, 2u, 3, 4, 5, 6, 7, 'Z');
    check("mixed types crossing the boundary", buf, "str -1 2 3 4 5 6 7 Z");

    // A pointer argument past the register block, which is the case that matters for
    // kernel calls: NtCreateFile takes eleven arguments and several are pointers.
    sprintf(buf, "%d %d %d %d %d %d %d %d %s", 1, 2, 3, 4, 5, 6, 7, 8, "tail");
    check("a pointer in the ninth slot", buf, "1 2 3 4 5 6 7 8 tail");

    // Our own variadic function, handing its va_list straight to Microsoft's vsprintf.
    con_printf(buf, "%d %s %d %d %d %d %d %d %d %d",
               1, "two", 3, 4, 5, 6, 7, 8, 9, 10);
    check("our va_list walked by Microsoft's vsprintf", buf, "1 two 3 4 5 6 7 8 9 10");

    // Our own va_arg, reading arguments our own compiler placed.
    sprintf(buf, "%d", sum(12, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12));
    check("our own va_arg walk over twelve ints", buf, "78");

    // The convention itself, checked against a callee that was written to it by hand
    // rather than generated. This is the case that is easy to get wrong: a float
    // that consumes its argument slot, so every integer after it moves up a register.
    {
        int mask = abi_probe(1, 2.0f, 3, 4, 5, 6, 7, 8, 9);
        int bit;
        run++;
        if (mask == PROBE_ALL) {
            DbgPrint("  ok    every argument arrived where Xenon puts it\n");
        } else {
            failed++;
            DbgPrint("  FAIL  arguments in the wrong place:");
            for (bit = 0; bit < 9; bit++)
                if (!(mask & (1 << bit)))
                    DbgPrint(bit == 1 ? " arg2(float)" :
                             bit == 8 ? " arg9(stack)" : " arg");
            DbgPrint("\n");
        }
    }

    DbgPrint("\n");
    DbgPrint(failed ? "RESULT: FAIL\n" : "RESULT: PASS\n");

    // The debug channel is dropped the moment the console reboots, which loses the last
    // few lines. Idle for a couple of seconds so they reach the host first.
    {
        volatile int spin;
        for (spin = 0; spin < 60000000; spin++)
            ;
    }

    DbgPrint("rebooting\n\n");
    HalReturnToFirmware(1);
    return 0;
}
