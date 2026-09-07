// Pass through the XDK <locale.h> and add the two _l functions it lacks.
//
// libc++'s Windows locale backend calls the MSVC locale-specific function family. The
// 360 XDK ships nearly all of it (_isupper_l, _iswctype_l, _strtod_l and the rest)
// so unlike the original Xbox this needs almost nothing. What is missing is the float
// and long double strtod variants, which arrived with the UCRT. Both widen to the
// double form the XDK does have; on this chip long double is double anyway.
#ifndef OXDK360_CSHIM_LOCALE_H
#define OXDK360_CSHIM_LOCALE_H

#include_next <locale.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

static __inline float _strtof_l(const char *nptr, char **endptr, _locale_t loc) {
    return (float)_strtod_l(nptr, endptr, loc);
}

static __inline long double _strtold_l(const char *nptr, char **endptr, _locale_t loc) {
    return (long double)_strtod_l(nptr, endptr, loc);
}

#ifdef __cplusplus
}
#endif

#endif
