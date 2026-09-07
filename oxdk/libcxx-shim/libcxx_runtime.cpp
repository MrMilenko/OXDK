// Runtime pieces libc++ normally ships prebuilt in libc++.a. There's no such
// library for the i386-xbox target, so provide the handful we need here:
// the forced std::string instantiation plus a few out-of-line helpers.
// Compile this in libc++ mode (OXDK_LIBCXX=1) alongside the rest of the app.
#include <cstddef>
#include <string>

// std::string members are declared `extern template` (expected from the
// dylib). Instantiate them here so they resolve at link time.
template class std::basic_string<char>;

namespace std {
inline namespace __1 {

// unordered_map bucket sizing. A plain next-prime is enough.
size_t __next_prime(size_t __n) {
    if (__n <= 2)
        return 2;
    __n |= 1;
    for (;; __n += 2) {
        bool __prime = true;
        for (size_t __i = 3; __i * __i <= __n; __i += 2) {
            if (__n % __i == 0) {
                __prime = false;
                break;
            }
        }
        if (__prime)
            return __n;
    }
}

// Under -fno-exceptions libc++ routes throws and failed assertions here.
[[noreturn]] void __libcpp_verbose_abort(const char*, ...) noexcept { __builtin_trap(); }

// std::to_string is declared in <string> but defined in libc++.a. Provide the
// integer overloads here, built without any CRT formatting calls.
namespace {
template <class U>
basic_string<char> uint_to_string(U v, bool neg) {
    char buf[24];
    int i = static_cast<int>(sizeof(buf));
    do {
        buf[--i] = static_cast<char>('0' + (v % 10));
        v /= 10;
    } while (v != 0);
    if (neg) {
        buf[--i] = '-';
    }
    return basic_string<char>(&buf[i], sizeof(buf) - i);
}
} // namespace

string to_string(int v) { return uint_to_string(v < 0 ? 0u - static_cast<unsigned>(v) : static_cast<unsigned>(v), v < 0); }
string to_string(unsigned v) { return uint_to_string(v, false); }
string to_string(long v) { return uint_to_string(v < 0 ? 0ul - static_cast<unsigned long>(v) : static_cast<unsigned long>(v), v < 0); }
string to_string(unsigned long v) { return uint_to_string(v, false); }
string to_string(long long v) { return uint_to_string(v < 0 ? 0ull - static_cast<unsigned long long>(v) : static_cast<unsigned long long>(v), v < 0); }
string to_string(unsigned long long v) { return uint_to_string(v, false); }

} // namespace __1
} // namespace std

// C++14 sized delete; the XDK CRT ships only the unsized operator delete.
void operator delete(void* __p, std::size_t) noexcept { ::operator delete(__p); }

// C99 snprintf / vsnprintf. Declared in the OXDK <stdio.h> cshim; implemented
// here over MSVC 7.1's _vsnprintf, with the C99 null-terminate-on-truncation
// guarantee. CRT functions are __cdecl (variadics can't be stdcall and the
// XDK libcmt symbols are cdecl-decorated), so override the project default.
#include <cstdarg>
extern "C" int __cdecl _vsnprintf(char *buf, std::size_t n, const char *fmt, std::va_list ap);

extern "C" int __cdecl vsnprintf(char *buf, std::size_t n, const char *fmt, std::va_list ap) {
    int r = _vsnprintf(buf, n, fmt, ap);
    if (n > 0) {
        buf[n - 1] = '\0';
    }
    return r;
}

extern "C" int __cdecl snprintf(char *buf, std::size_t n, const char *fmt, ...) {
    std::va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(buf, n, fmt, ap);
    va_end(ap);
    return r;
}

// C99 float-suffix math overloads + trunc; the XDK CRT is C95-era and ships
// only the double versions. The float ones widen to double for accuracy.
#include <math.h>
extern "C" float  __cdecl expf(float x)  { return (float)exp((double)x); }
extern "C" float  __cdecl logf(float x)  { return (float)log((double)x); }
extern "C" float  __cdecl ldexpf(float x, int e) { return (float)ldexp((double)x, e); }
extern "C" double __cdecl trunc(double x) { return x >= 0.0 ? floor(x) : ceil(x); }

// Xbox has no environment block; the conventional "no such var" return is NULL.
extern "C" char* __cdecl getenv(const char*) { return 0; }

// POSIX I/O that the XDK CRT omits. Engines using these typically wire real
// file access through xboxkrnl elsewhere; these stubs let unused code paths
// link and fail predictably if reached (-1 / errno set).
#include <errno.h>
extern "C" int  __cdecl access(const char*, int)               { errno = ENOENT; return -1; }
extern "C" int  __cdecl chdir(const char*)                     { errno = ENOENT; return -1; }
extern "C" int  __cdecl close(int)                             { return 0; }
extern "C" char* __cdecl getcwd(char*, int)                    { return 0; }
extern "C" long __cdecl lseek(int, long, int)                  { return -1; }
extern "C" int  __cdecl mkdir(const char*)                     { errno = EACCES; return -1; }
extern "C" int  __cdecl open(const char*, int, ...)            { errno = ENOENT; return -1; }
extern "C" int  __cdecl read(int, void*, unsigned)             { return -1; }
extern "C" int  __cdecl write(int, const void*, unsigned)      { return -1; }

// nothrow placement new. Routes to global operator new; under -fno-exceptions
// the allocator aborts on failure, so we never actually return null here.
#include <new>
void* operator new(std::size_t sz, const std::nothrow_t&) noexcept {
    return ::operator new(sz);
}

// Runtime pieces from libcxx/src/. Group: __hash_memory (newer libc++ gates
// it behind a vendor-availability macro), the shared_ptr control blocks, and
// recursive_mutex out-of-line members.
#include <memory>
#include <mutex>
#include <typeinfo>
#include <windows.h>

namespace std {
inline namespace __1 {

[[gnu::pure]] size_t __hash_memory(_LIBCPP_NOESCAPE const void* __p, size_t __n) noexcept {
    const unsigned char* __b = static_cast<const unsigned char*>(__p);
    size_t __h = 0x811C9DC5u;
    for (size_t __i = 0; __i < __n; ++__i) {
        __h ^= __b[__i];
        __h *= 16777619u;
    }
    return __h;
}

__shared_count::~__shared_count() {}

__shared_weak_count::~__shared_weak_count() {}

void __shared_weak_count::__release_weak() noexcept {
    if (__libcpp_atomic_refcount_decrement(__shared_weak_owners_) == -1) {
        __on_zero_shared_weak();
    }
}

__shared_weak_count* __shared_weak_count::lock() noexcept {
    long __owners = __atomic_load_n(&__shared_owners_, __ATOMIC_RELAXED);
    while (__owners != -1) {
        if (__atomic_compare_exchange_n(&__shared_owners_, &__owners,
                                        __owners + 1, true,
                                        __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
            return this;
        }
    }
    return nullptr;
}

const void* __shared_weak_count::__get_deleter(const type_info&) const noexcept {
    return nullptr;
}

recursive_mutex::recursive_mutex() { __libcpp_recursive_mutex_init(&__m_); }
recursive_mutex::~recursive_mutex() { __libcpp_recursive_mutex_destroy(&__m_); }
void recursive_mutex::lock() { __libcpp_recursive_mutex_lock(&__m_); }
bool recursive_mutex::try_lock() noexcept { return __libcpp_recursive_mutex_trylock(&__m_); }
void recursive_mutex::unlock() noexcept { __libcpp_recursive_mutex_unlock(&__m_); }

// Win32 thread-backend wrappers. The opaque void*[6] storage on x86 is the
// same 24 bytes as CRITICAL_SECTION, so we reinterpret in place.
static inline CRITICAL_SECTION* __as_cs(__libcpp_recursive_mutex_t* __m) {
    return reinterpret_cast<CRITICAL_SECTION*>(__m);
}

int __libcpp_recursive_mutex_init(__libcpp_recursive_mutex_t* __m) {
    InitializeCriticalSection(__as_cs(__m));
    return 0;
}
int __libcpp_recursive_mutex_lock(__libcpp_recursive_mutex_t* __m) {
    EnterCriticalSection(__as_cs(__m));
    return 0;
}
bool __libcpp_recursive_mutex_trylock(__libcpp_recursive_mutex_t* __m) {
    return TryEnterCriticalSection(__as_cs(__m)) != FALSE;
}
int __libcpp_recursive_mutex_unlock(__libcpp_recursive_mutex_t* __m) {
    LeaveCriticalSection(__as_cs(__m));
    return 0;
}
int __libcpp_recursive_mutex_destroy(__libcpp_recursive_mutex_t* __m) {
    DeleteCriticalSection(__as_cs(__m));
    return 0;
}

} // namespace __1

// std::type_info out-of-line members. Under MSVC ABI (_LIBCPP_ABI_MICROSOFT)
// libc++'s type_info stores a decorated name in __data.__decorated_name and
// expects these four to be defined externally. dynamic_cast / typeid still
// route through libcmt's __RTDynamicCast etc.; these methods are what
// libc++'s own STL uses to compare and identify types in hash maps and the
// like.
#include <string.h>
type_info::~type_info() {}

int type_info::__compare(const type_info& __rhs) const noexcept {
    return strcmp(__data.__decorated_name, __rhs.__data.__decorated_name);
}

const char* type_info::name() const noexcept {
    return __data.__decorated_name;
}

size_t type_info::hash_code() const noexcept {
    // FNV-1a over the decorated name; deterministic per type.
    size_t h = 0x811C9DC5u;
    for (const char* p = __data.__decorated_name; *p != '\0'; ++p) {
        h ^= (unsigned char)*p;
        h *= 16777619u;
    }
    return h;
}

} // namespace std
