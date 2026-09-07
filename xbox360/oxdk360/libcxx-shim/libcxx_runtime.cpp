// The pieces libc++ expects to find in its prebuilt library.
//
// libc++ ships most of itself in headers, but a handful of entities are declared in
// them and defined in libc++.a: the out-of-line std::string members, the bucket sizing
// helper the unordered containers use, and the abort hook its hardened checks call.
// There is no libc++.a for this target, so they are supplied here. Built automatically
// with OXDK360_LIBCXX=1; see oxdk360.mk.

#include <cstdarg>
#include <cstddef>
#include <string>

// For the FormatMessage flags and LocalAlloc; the cshim routes this to <xtl.h>.
#include <windows.h>

extern "C" void DbgPrint(const char *fmt, ...);
extern "C" void HalReturnToFirmware(unsigned int routine);
extern "C" int __cdecl _vsnprintf(char *buf, std::size_t n, const char *fmt, std::va_list ap);
extern "C" int __cdecl _snprintf(char *buf, std::size_t n, const char *fmt, ...);

// Several basic_string<char> members are declared extern template on the assumption
// that the library holds them. Instantiating the class here defines them in this
// object instead, which is the whole libc++ bargain: every entity is one we compiled.
template class std::basic_string<char>;

namespace std {
inline namespace __1 {

// How the unordered containers size their bucket array. A plain next-prime will do.
size_t __next_prime(size_t n) {
    if (n <= 2)
        return 2;
    n |= 1;
    for (;; n += 2) {
        bool prime = true;
        for (size_t i = 3; i * i <= n; i += 2) {
            if (n % i == 0) {
                prime = false;
                break;
            }
        }
        if (prime)
            return n;
    }
}

// What libc++ calls when a hardened check fails: an out-of-range index, a bad iterator.
// Printing before stopping is the point, since the alternative on a console is a lockup
// with nothing said. Reboots to the dashboard rather than spinning, so the box stays
// reachable over XBDM.
[[noreturn]] void __libcpp_verbose_abort(const char *fmt, ...) noexcept {
    char msg[512];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    msg[sizeof msg - 1] = '\0';

    DbgPrint("libc++: %s\n", msg);
    HalReturnToFirmware(1);
    for (;;) {
    }
}

} // namespace __1
} // namespace std

// The UCRT internal libc++'s locale backend formats through. Single locale on a
// console, so the handle is ignored; what matters is the return value, which libc++
// uses to size buffers and therefore has to be the C99 one, the length the output
// would have taken, rather than MSVC's -1 on truncation.
extern "C" int __cdecl _vscprintf(const char *fmt, std::va_list ap);

extern "C" int __cdecl __stdio_common_vsprintf(unsigned long long, char *buf, std::size_t n,
                                               const char *fmt, _locale_t, std::va_list ap) {
    std::va_list copy;
    va_copy(copy, ap);
    int needed = _vscprintf(fmt, copy);
    va_end(copy);

    int written = _vsnprintf(buf, n, fmt, ap);
    if (n > 0 && written < 0)
        buf[n - 1] = '\0';
    return needed;
}

// std::string's out-of-line members again, for wide strings. libc++'s locale code
// instantiates wstring whether or not a title ever asks for one.
template class std::basic_string<wchar_t>;

// concatenation is out of line too, and lives in libc++'s string.cpp, which does not
// build here: it needs charconv headers that are not shipped with the release headers.
template std::string std::operator+ <char, std::char_traits<char>, std::allocator<char> >(
    const char *, const std::string &);

// The locale handle API. The XDK declares all three and implements none of them: a
// console has one locale, so there is nothing to create or switch to. Returning null is
// what the CRT's own _l functions treat as "use the current locale".
extern "C" {
_locale_t __cdecl _create_locale(int, const char *) { return 0; }
void __cdecl _free_locale(_locale_t) {}
int __cdecl _configthreadlocale(int) { return 0; }

// FormatMessageA over what the console actually has. libc++ asks for an allocated
// buffer and frees it with LocalFree, so honour that; there is no message table to
// look anything up in, so the number is the message.
unsigned long __stdcall FormatMessageA(unsigned long flags, const void *, unsigned long messageId,
                                       unsigned long, char *buffer, unsigned long size,
                                       std::va_list *) {
    char msg[64];
    int n = _snprintf(msg, sizeof msg, "error %lu", messageId);
    if (n < 0)
        n = 0;
    msg[sizeof msg - 1] = '\0';

    if (flags & FORMAT_MESSAGE_ALLOCATE_BUFFER) {
        char *out = (char *)LocalAlloc(0x0040 /* LPTR */, (std::size_t)n + 1);
        if (!out)
            return 0;
        for (int i = 0; i <= n; i++)
            out[i] = msg[i];
        *(char **)buffer = out;
        return (unsigned long)n;
    }

    if (size == 0)
        return 0;
    unsigned long copied = 0;
    while (copied + 1 < size && msg[copied]) {
        buffer[copied] = msg[copied];
        copied++;
    }
    buffer[copied] = '\0';
    return copied;
}
}

// What vcruntime would provide, on a console that has no vcruntime.
//
// libc++'s exception.cpp and its exception_ptr are written against Microsoft's C++
// runtime, and pull these in whether or not a title ever throws. Titles here are built
// -fno-exceptions, so nothing can construct a live exception_ptr and none of the copy
// or rethrow paths is reachable; what matters is that the handful of names resolve and
// that anything genuinely unreachable stops rather than continuing on bad state.
namespace {
void (*oxdk360_terminate_handler)() = 0;
void (*oxdk360_unexpected_handler)() = 0;
} // namespace

extern "C" {
void (*__cdecl set_terminate(void (*handler)()) throw())() {
    void (*old)() = oxdk360_terminate_handler;
    oxdk360_terminate_handler = handler;
    return old;
}

void (*__cdecl set_unexpected(void (*handler)()) throw())() {
    void (*old)() = oxdk360_unexpected_handler;
    oxdk360_unexpected_handler = handler;
    return old;
}

// Nothing is ever in flight without exceptions.
int __cdecl __uncaught_exceptions() { return 0; }
}

// exception_ptr is two pointers; these keep both defined and empty.
namespace {
struct oxdk360_exception_ptr {
    void *p1;
    void *p2;
};
} // namespace

void __cdecl __ExceptionPtrCreate(void *self) {
    oxdk360_exception_ptr *e = (oxdk360_exception_ptr *)self;
    e->p1 = 0;
    e->p2 = 0;
}
void __cdecl __ExceptionPtrDestroy(void *) {}
void __cdecl __ExceptionPtrCopy(void *self, const void *other) {
    *(oxdk360_exception_ptr *)self = *(const oxdk360_exception_ptr *)other;
}
void __cdecl __ExceptionPtrAssign(void *self, const void *other) {
    *(oxdk360_exception_ptr *)self = *(const oxdk360_exception_ptr *)other;
}
bool __cdecl __ExceptionPtrCompare(const void *a, const void *b) {
    const oxdk360_exception_ptr *x = (const oxdk360_exception_ptr *)a;
    const oxdk360_exception_ptr *y = (const oxdk360_exception_ptr *)b;
    return x->p1 == y->p1 && x->p2 == y->p2;
}
bool __cdecl __ExceptionPtrToBool(const void *self) {
    return ((const oxdk360_exception_ptr *)self)->p1 != 0;
}
void __cdecl __ExceptionPtrSwap(void *a, void *b) {
    oxdk360_exception_ptr t = *(oxdk360_exception_ptr *)a;
    *(oxdk360_exception_ptr *)a = *(oxdk360_exception_ptr *)b;
    *(oxdk360_exception_ptr *)b = t;
}
void __cdecl __ExceptionPtrCurrentException(void *self) { __ExceptionPtrCreate(self); }
void __cdecl __ExceptionPtrCopyException(void *self, const void *, const void *) {
    __ExceptionPtrCreate(self);
}
[[noreturn]] void __cdecl __ExceptionPtrRethrow(const void *) {
    // Reachable only by rethrowing a captured exception, which cannot exist here.
    DbgPrint("libc++: rethrow with exceptions disabled\n");
    std::terminate();
}

// C++14 sized delete. The XDK CRT predates it and oxdk360/builtins supplies only the
// unsized forms, so route these to those.
void operator delete(void *p, std::size_t) noexcept { ::operator delete(p); }
void operator delete[](void *p, std::size_t) noexcept { ::operator delete[](p); }

// The C99 float math the 360 CRT does not ship: it has the double routines only, and
// libc++'s <cmath> overloads resolve onto these names. Each widens to double, which
// costs nothing on a chip whose FPU is double internally anyway.
//
// Weak, because the CRT is not the only place these can come from: xaudio2.lib carries
// its own sinf, cosf, sqrtf and floorf inside its objects, and a title that links audio
// would otherwise get a duplicate symbol for each. Weak means a real implementation
// always wins and these only fill what nothing else provides.
#define OXDK360_WEAK __attribute__((weak))
#include <math.h>
extern "C" {
OXDK360_WEAK float __cdecl acosf(float x)           { return (float)acos((double)x); }
OXDK360_WEAK float __cdecl asinf(float x)           { return (float)asin((double)x); }
OXDK360_WEAK float __cdecl atanf(float x)           { return (float)atan((double)x); }
OXDK360_WEAK float __cdecl atan2f(float y, float x) { return (float)atan2((double)y, (double)x); }
OXDK360_WEAK float __cdecl ceilf(float x)           { return (float)ceil((double)x); }
OXDK360_WEAK float __cdecl cosf(float x)            { return (float)cos((double)x); }
OXDK360_WEAK float __cdecl coshf(float x)           { return (float)cosh((double)x); }
OXDK360_WEAK float __cdecl expf(float x)            { return (float)exp((double)x); }
OXDK360_WEAK float __cdecl fabsf(float x)           { return __builtin_fabsf(x); }
OXDK360_WEAK float __cdecl floorf(float x)          { return (float)floor((double)x); }
OXDK360_WEAK float __cdecl fmodf(float x, float y)  { return (float)fmod((double)x, (double)y); }
OXDK360_WEAK float __cdecl frexpf(float x, int *e)  { return (float)frexp((double)x, e); }
OXDK360_WEAK float __cdecl ldexpf(float x, int e)   { return (float)ldexp((double)x, e); }
OXDK360_WEAK float __cdecl logf(float x)            { return (float)log((double)x); }
OXDK360_WEAK float __cdecl log10f(float x)          { return (float)log10((double)x); }
OXDK360_WEAK float __cdecl powf(float x, float y)   { return (float)pow((double)x, (double)y); }
OXDK360_WEAK float __cdecl sinf(float x)            { return (float)sin((double)x); }
OXDK360_WEAK float __cdecl sinhf(float x)           { return (float)sinh((double)x); }
OXDK360_WEAK float __cdecl sqrtf(float x)           { return (float)sqrt((double)x); }
OXDK360_WEAK float __cdecl tanf(float x)            { return (float)tan((double)x); }
OXDK360_WEAK float __cdecl tanhf(float x)           { return (float)tanh((double)x); }

OXDK360_WEAK float __cdecl modff(float x, float *ip) {
    double i;
    float f = (float)modf((double)x, &i);
    *ip = (float)i;
    return f;
}
}
