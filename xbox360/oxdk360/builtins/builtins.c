/* Compiler runtime helpers for 64-bit arithmetic.
 *
 * clang targets Xenon as 32-bit PowerPC, so a long long divide or an int64 to double
 * conversion becomes a call into compiler-rt. Microsoft's compiler never needs these:
 * it treats the registers as the 64 bits they physically are and does the work inline,
 * so the XDK ships neither clang's names nor its own (_alldiv, _allmul). Nothing
 * provides them, and a link that needs one simply fails.
 *
 * Everything here is written with 32-bit operations and shifts so that none of it
 * lowers to a call to the helpers it is defining.
 */

typedef unsigned long long u64;
typedef long long          s64;
typedef unsigned int       u32;

static u64 udivmod(u64 n, u64 d, u64 *rem)
{
    u64 q = 0, r = 0;
    int i;

    if (d == 0) {                       /* undefined; give something predictable */
        if (rem) *rem = 0;
        return 0;
    }
    for (i = 63; i >= 0; i--) {
        r = (r << 1) | ((n >> i) & 1);
        if (r >= d) {
            r -= d;
            q |= (u64)1 << i;
        }
    }
    if (rem) *rem = r;
    return q;
}

u64 __udivdi3(u64 a, u64 b)  { return udivmod(a, b, 0); }
u64 __umoddi3(u64 a, u64 b)  { u64 r; udivmod(a, b, &r); return r; }

s64 __divdi3(s64 a, s64 b)
{
    int neg = 0;
    u64 q;

    if (a < 0) { a = -a; neg ^= 1; }
    if (b < 0) { b = -b; neg ^= 1; }
    q = udivmod((u64)a, (u64)b, 0);
    return neg ? -(s64)q : (s64)q;
}

s64 __moddi3(s64 a, s64 b)
{
    int neg = 0;
    u64 r;

    if (a < 0) { a = -a; neg = 1; }
    if (b < 0) { b = -b; }
    udivmod((u64)a, (u64)b, &r);
    return neg ? -(s64)r : (s64)r;
}

/* The halves go through (double)(int), which the hardware does directly, rather than
   an unsigned conversion that would itself want a helper. Values above 2^53 round, as
   they must: a double has 53 bits of mantissa. */
static double u32_to_double(u32 v)
{
    double d = (double)(int)v;
    return d < 0 ? d + 4294967296.0 : d;
}

double __floatundidf(u64 a)
{
    return u32_to_double((u32)(a >> 32)) * 4294967296.0 + u32_to_double((u32)a);
}

double __floatdidf(s64 a)
{
    return a < 0 ? -__floatundidf((u64)(-a)) : __floatundidf((u64)a);
}

float __floatdisf(s64 a)   { return (float)__floatdidf(a); }
float __floatundisf(u64 a) { return (float)__floatundidf(a); }

static u64 double_to_u64(double d)
{
    u64 hi;
    double rest;

    if (d < 1.0) return 0;
    hi = (u64)(u32)(int)(d / 4294967296.0);
    rest = d - (double)(s64)(hi << 32);
    if (rest < 0) rest = 0;
    return (hi << 32) | (u32)(int)rest;
}

s64 __fixdfdi(double d)
{
    if (d >= 9223372036854775808.0)  return (s64)0x7FFFFFFFFFFFFFFFULL;
    if (d <= -9223372036854775808.0) return (s64)0x8000000000000000ULL;
    return d < 0 ? -(s64)double_to_u64(-d) : (s64)double_to_u64(d);
}

s64 __fixsfdi(float f)          { return __fixdfdi((double)f); }
u64 __fixunsdfdi(double d)      { return d <= 0 ? 0 : double_to_u64(d); }
u64 __fixunssfdi(float f)       { return __fixunsdfdi((double)f); }

/* Referenced by the unwind tables clang emits for C++ code built with exceptions.
   Nothing here unwinds: the XDK provides no personality routine, and a title that
   actually throws has no handler to reach. Defining it lets such code link; reaching
   it would mean an exception was thrown with nothing to catch it. */
void __gxx_personality_v0(void) { for (;;) { } }

/* Itanium C++ ABI runtime.
 *
 * clang emits calls to these for ordinary C++: a function-local static needs a guard,
 * a class with pure virtuals needs a trap, and anything built with exceptions needs
 * the throw path to exist. Microsoft's compiler uses a different scheme entirely, so
 * the XDK supplies none of them and a C++ title will not link without these.
 *
 * The guards are the single-threaded form. A title that initialises a function-local
 * static from two threads at once would need a real one; the Itanium guard's first
 * byte is the initialised flag, which is what these read and write.
 */
int  __cxa_guard_acquire(unsigned long long *g) { return *(volatile char *)g == 0; }
void __cxa_guard_release(unsigned long long *g) { *(volatile char *)g = 1; }
void __cxa_guard_abort(unsigned long long *g)   { (void)g; }

/* Reached only by calling a pure virtual through a partially constructed object.
   There is nowhere useful to go from here. */
void __cxa_pure_virtual(void) { for (;;) { } }

/* No unwinder: __gxx_personality_v0 above cannot unwind either, so a throw has no
   handler to reach and stopping here is the honest outcome. */
void  __cxa_throw(void *o, void *t, void (*d)(void *)) { (void)o; (void)t; (void)d; for (;;) { } }
void *__cxa_get_exception_ptr(void *p) { return p; }
void *__cxa_begin_catch(void *p)       { return p; }
void  __cxa_end_catch(void)            { }

/* Static destructor registration. A title reboots rather than returning from main, so
   nothing here ever runs the handlers; recording them would only keep them alive. */
int __cxa_atexit(void (*f)(void *), void *arg, void *dso) { (void)f; (void)arg; (void)dso; return 0; }

/* The rest of the throw path. Without an unwinder there is nothing to allocate an
   exception for, but the calls have to resolve for C++ to link. */
void *__cxa_allocate_exception(unsigned long size) { (void)size; return 0; }
void  __cxa_free_exception(void *p)                { (void)p; }
void  __cxa_call_unexpected(void *p)               { (void)p; for (;;) { } }

/* An intrinsic Microsoft's compiler folds away at compile time. Nothing here can fold,
   and no caller depends on the answer being anything but conservative. */
int __IsIntConst(int v) { (void)v; return 0; }

/* operator new and delete.
 *
 * clang mangles these the Itanium way (_Znwm, _Znam, _ZdlPv) while libcpMT.lib holds
 * MSVC's spelling (??2@YAPAXI@Z), so the XDK's copies are unreachable and every C++
 * title needs its own. Declared through asm labels so this stays a C file: defining
 * them as real operators would need a second translation unit and a second build rule.
 */
void *malloc(unsigned long);
void  free(void *);

void *oxdk_new(unsigned long n)    __asm__("_Znwm");
void *oxdk_new_array(unsigned long n) __asm__("_Znam");
void  oxdk_delete(void *p)         __asm__("_ZdlPv");
/* C++14 sized delete. Weak so a C++ library that defines its own wins; without it any C++
   title that deletes through a complete type fails to link. */
__attribute__((weak)) void oxdk_delete_sized(void *p, unsigned long n) __asm__("_ZdlPvm");
__attribute__((weak)) void oxdk_delete_sized_arr(void *p, unsigned long n) __asm__("_ZdaPvm");
void  oxdk_delete_array(void *p)   __asm__("_ZdaPv");

void *oxdk_new(unsigned long n)       { return malloc(n); }
void *oxdk_new_array(unsigned long n) { return malloc(n); }
void  oxdk_delete(void *p)            { free(p); }
void  oxdk_delete_array(void *p)      { free(p); }
__attribute__((weak)) void oxdk_delete_sized(void *p, unsigned long n)     { (void)n; free(p); }
__attribute__((weak)) void oxdk_delete_sized_arr(void *p, unsigned long n) { (void)n; free(p); }

/* RTTI class hierarchy vtables.
 *
 * clang emits a reference to one of these from every type_info it generates, so any C++
 * built with -frtti needs them present. libcpMT has Microsoft's RTTI instead, which uses
 * a different layout entirely and different names.
 *
 * These are placeholders: enough for typeid to link and to compare types by pointer,
 * which is what most code does. dynamic_cast across a hierarchy walks these vtables and
 * would need the real __cxxabiv1 implementation, so it will not work.
 */
static void *class_type_info_vtable[4]     = { 0, 0, 0, 0 };
static void *si_class_type_info_vtable[4]  = { 0, 0, 0, 0 };
static void *vmi_class_type_info_vtable[4] = { 0, 0, 0, 0 };

void *_ZTVN10__cxxabiv117__class_type_infoE[1]    __asm__("_ZTVN10__cxxabiv117__class_type_infoE");
void *_ZTVN10__cxxabiv120__si_class_type_infoE[1] __asm__("_ZTVN10__cxxabiv120__si_class_type_infoE");
void *_ZTVN10__cxxabiv121__vmi_class_type_infoE[1] __asm__("_ZTVN10__cxxabiv121__vmi_class_type_infoE");

void *_ZTVN10__cxxabiv117__class_type_infoE[1]     = { class_type_info_vtable };
void *_ZTVN10__cxxabiv120__si_class_type_infoE[1]  = { si_class_type_info_vtable };
void *_ZTVN10__cxxabiv121__vmi_class_type_infoE[1] = { vmi_class_type_info_vtable };

static void *enum_type_info_vtable[4] = { 0, 0, 0, 0 };
void *_ZTVN10__cxxabiv116__enum_type_infoE[1] __asm__("_ZTVN10__cxxabiv116__enum_type_infoE");
void *_ZTVN10__cxxabiv116__enum_type_infoE[1] = { enum_type_info_vtable };

/* Three more that fall in the gap between clang and Microsoft's C++ library.
 *
 * std::_Fiopen is what MSVC's fstream calls to open a file. It is declared in the
 * headers we compile with clang, so the call is mangled the Itanium way, while
 * libcpMT.lib holds it under MSVC's mangling. The two never meet, so the open is
 * reimplemented here on top of fopen. The mode bits are MSVC's ios_base::openmode.
 */
void *fopen(const char *, const char *);

void *oxdk_Fiopen(const char *name, int mode, int prot) __asm__("_ZSt7_FiopenPKcii");
void *oxdk_Fiopen(const char *name, int mode, int prot)
{
    const char *m;

    (void)prot;
    if (mode & 0x10)                      /* trunc */
        m = (mode & 0x01) ? "w+b" : "wb";
    else if (mode & 0x08)                 /* app */
        m = (mode & 0x01) ? "a+b" : "ab";
    else if ((mode & 0x02) && (mode & 0x01))
        m = "r+b";
    else if (mode & 0x02)                 /* out */
        m = "wb";
    else
        m = "rb";
    return fopen(name, m);
}

/* Reached when a throw finds no handler. There is no unwinder here, so this is where
   such a program stops. */
/* Weak: in libc++ mode the real std::terminate comes from libc++'s exception.cpp, and
   a strong definition here would collide with it. This one is the fallback for titles
   built against the XDK's own C++ library, which does not ship one clang can call. */
__attribute__((weak)) void oxdk_terminate(void) __asm__("_ZSt9terminatev");
__attribute__((weak)) void oxdk_terminate(void) { for (;;) { } }

/* type_info for int. Placeholder, like the class hierarchy vtables above: enough for
   typeid(int) to link and compare by address. */
static void *int_type_info[2] = { 0, 0 };
void *_ZTIi[1] __asm__("_ZTIi");
void *_ZTIi[1] = { int_type_info };

/* More of the same gap. Every out-of-line entity in Microsoft's C++ library is declared
   in headers we compile with clang and defined in libcpMT under MSVC's mangling, so each
   one a title happens to touch needs a stand-in here. The set is open ended: it grows
   with whatever the title uses, which is an argument for keeping projects off iostreams
   rather than for finishing this list. */
void oxdk_Xout_of_range(const char *w) __asm__("_ZSt14_Xout_of_rangePKc");
void oxdk_Xout_of_range(const char *w) { (void)w; for (;;) { } }

void oxdk_Xlength_error(const char *w) __asm__("_ZSt14_Xlength_errorPKc");
void oxdk_Xlength_error(const char *w) { (void)w; for (;;) { } }

static void *iostream_category_object[2] = { 0, 0 };
void *oxdk_iostream_category(void) __asm__("_ZSt17iostream_categoryv");
void *oxdk_iostream_category(void) { return iostream_category_object; }

/* std::_BADOFF, the streamoff value meaning "no position". */
long long _ZSt7_BADOFF __asm__("_ZSt7_BADOFF");
long long _ZSt7_BADOFF = -1;

/* The C99 spelling. libcMT has _snprintf; the librarian's alias member for the
   unprefixed name is one of the machine 0x0000 records coff2elf declines, and unlike
   the oldnames aliases it is not in a library titles normally link. */
int _snprintf(char *, unsigned long, const char *, ...);
int oxdk_snprintf(char *b, unsigned long n, const char *f, ...) __asm__("snprintf");
int oxdk_snprintf(char *b, unsigned long n, const char *f, ...)
{
    __builtin_va_list ap;
    int r;
    __builtin_va_start(ap, f);
    r = __builtin_vsnprintf(b, n, f, ap);
    __builtin_va_end(ap);
    return r;
}
