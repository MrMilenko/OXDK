// XDK predates MSVC's locale-specific _l function family (_strtof_l,
// _iswalpha_l, etc.) that libc++'s Windows locale backend depends on.
// We're single-locale on Xbox so each _l variant just forwards to the
// non-_l version, ignoring the locale handle. The handle itself becomes
// an opaque void*.
#ifndef OXDK_CSHIM_LOCALE_H
#define OXDK_CSHIM_LOCALE_H

#include_next <locale.h>
#include <ctype.h>
#include <wctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <wchar.h>

#ifndef _LOCALE_T_DEFINED
#define _LOCALE_T_DEFINED
typedef void* _locale_t;
#endif

#ifdef __cplusplus
extern "C" {
#endif

static __inline int  _isupper_l (int c,   _locale_t) { return isupper(c); }
static __inline int  _tolower_l (int c,   _locale_t) { return tolower(c); }
static __inline int  _toupper_l (int c,   _locale_t) { return toupper(c); }
static __inline int  _iswalpha_l (wint_t c,            _locale_t) { return iswalpha(c); }
static __inline int  _iswcntrl_l (wint_t c,            _locale_t) { return iswcntrl(c); }
static __inline int  _iswctype_l (wint_t c, wctype_t t,_locale_t) { return iswctype(c, t); }
static __inline int  _iswdigit_l (wint_t c,            _locale_t) { return iswdigit(c); }
static __inline int  _iswlower_l (wint_t c,            _locale_t) { return iswlower(c); }
static __inline int  _iswprint_l (wint_t c,            _locale_t) { return iswprint(c); }
static __inline int  _iswpunct_l (wint_t c,            _locale_t) { return iswpunct(c); }
static __inline int  _iswspace_l (wint_t c,            _locale_t) { return iswspace(c); }
static __inline int  _iswupper_l (wint_t c,            _locale_t) { return iswupper(c); }
static __inline int  _iswxdigit_l(wint_t c,            _locale_t) { return iswxdigit(c); }
static __inline wint_t _towlower_l(wint_t c, _locale_t) { return towlower(c); }
static __inline wint_t _towupper_l(wint_t c, _locale_t) { return towupper(c); }

static __inline double      _strtod_l (const char* s, char** e, _locale_t) { return strtod(s, e); }
static __inline float       _strtof_l (const char* s, char** e, _locale_t) { return (float)strtod(s, e); }
static __inline long double _strtold_l(const char* s, char** e, _locale_t) { return strtod(s, e); }

static __inline int    _strcoll_l(const char* a, const char* b, _locale_t) { return strcmp(a, b); }
static __inline size_t _strxfrm_l(char* dst, const char* src, size_t n, _locale_t) {
    size_t l = strlen(src);
    if (l + 1 <= n) memcpy(dst, src, l + 1);
    return l;
}
static __inline int    _wcscoll_l(const wchar_t* a, const wchar_t* b, _locale_t) { return wcscmp(a, b); }
static __inline size_t _wcsxfrm_l(wchar_t* dst, const wchar_t* src, size_t n, _locale_t) {
    size_t l = wcslen(src);
    if (l + 1 <= n) memcpy(dst, src, (l + 1) * sizeof(wchar_t));
    return l;
}

static __inline int    _mbtowc_l (wchar_t* w, const char* s, size_t n, _locale_t) { return mbtowc(w, s, (int)n); }
static __inline size_t _strftime_l(char* s, size_t n, const char* fmt, const struct tm* t, _locale_t) {
    return strftime(s, n, fmt, t);
}

// vsscanf is the variadic-friendly form; XDK has it as _vsscanf, so define
// _sscanf_l in terms of it.
extern int __cdecl _vsscanf(const char*, const char*, va_list);
static __inline int _sscanf_l(const char* s, _locale_t, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = _vsscanf(s, fmt, ap);
    va_end(ap);
    return r;
}

#ifdef __cplusplus
}
#endif

#endif
