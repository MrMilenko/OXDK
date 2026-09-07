// Clean C <math.h> for libc++ mode.
//
// The XDK's own <math.h> adds Dinkumware C++ overloads (abs(double), pow(int),
// float/long-double inlines) that collide with libc++'s std::abs etc. This
// replacement exposes only the C interface; libc++ layers its own C++ overloads
// on top, exactly as on a normal platform. Suppressing the XDK header by its
// _INC_MATH guard keeps any sibling include from pulling the overloads back in.
#ifndef OXDK_CSHIM_MATH_H
#define OXDK_CSHIM_MATH_H
#define _INC_MATH

#ifdef __cplusplus
extern "C" {
#endif

extern double _HUGE;
#define HUGE_VAL _HUGE

#define EDOM   33
#define ERANGE 34

double __cdecl acos(double);
double __cdecl asin(double);
double __cdecl atan(double);
double __cdecl atan2(double, double);
double __cdecl cos(double);
double __cdecl cosh(double);
double __cdecl exp(double);
double __cdecl fabs(double);
double __cdecl floor(double);
double __cdecl fmod(double, double);
double __cdecl frexp(double, int*);
double __cdecl ldexp(double, int);
double __cdecl log(double);
double __cdecl log10(double);
double __cdecl modf(double, double*);
double __cdecl pow(double, double);
double __cdecl sin(double);
double __cdecl sinh(double);
double __cdecl sqrt(double);
double __cdecl tan(double);
double __cdecl tanh(double);
double __cdecl ceil(double);
double __cdecl _hypot(double, double);
double __cdecl _cabs(struct _complex);

float __cdecl fabsf(float);
float __cdecl sqrtf(float);

#ifdef __cplusplus
}
#endif

#define FP_NAN       0
#define FP_INFINITE  1
#define FP_ZERO      2
#define FP_SUBNORMAL 3
#define FP_NORMAL    4

#define INFINITY  __builtin_huge_valf()
#define NAN       __builtin_nanf("")
#define HUGE_VALF __builtin_huge_valf()
#define HUGE_VALL __builtin_huge_vall()

// In its MSVCRT mode libc++ expects the C runtime's <math.h> to supply the C99
// classification functions as overloads (the UCRT does); the 2002 XDK CRT does
// not, so provide them here from compiler builtins. cmath's `using ::isinf`
// then resolves.
#ifdef __cplusplus
inline bool isinf(float x)        { return __builtin_isinf(x); }
inline bool isinf(double x)       { return __builtin_isinf(x); }
inline bool isinf(long double x)  { return __builtin_isinf(x); }
inline bool isnan(float x)        { return __builtin_isnan(x); }
inline bool isnan(double x)       { return __builtin_isnan(x); }
inline bool isnan(long double x)  { return __builtin_isnan(x); }
inline bool isfinite(float x)       { return __builtin_isfinite(x); }
inline bool isfinite(double x)      { return __builtin_isfinite(x); }
inline bool isfinite(long double x) { return __builtin_isfinite(x); }
inline bool isnormal(float x)       { return __builtin_isnormal(x); }
inline bool isnormal(double x)      { return __builtin_isnormal(x); }
inline bool isnormal(long double x) { return __builtin_isnormal(x); }
inline bool signbit(float x)        { return __builtin_signbit(x); }
inline bool signbit(double x)       { return __builtin_signbit(x); }
inline bool signbit(long double x)  { return __builtin_signbit(x); }
inline int  fpclassify(float x)       { return __builtin_fpclassify(FP_NAN, FP_INFINITE, FP_NORMAL, FP_SUBNORMAL, FP_ZERO, x); }
inline int  fpclassify(double x)      { return __builtin_fpclassify(FP_NAN, FP_INFINITE, FP_NORMAL, FP_SUBNORMAL, FP_ZERO, x); }
inline int  fpclassify(long double x) { return __builtin_fpclassify(FP_NAN, FP_INFINITE, FP_NORMAL, FP_SUBNORMAL, FP_ZERO, x); }
#endif

#ifdef _USE_MATH_DEFINES
#define M_E        2.71828182845904523536
#define M_LOG2E    1.44269504088896340736
#define M_LOG10E   0.434294481903251827651
#define M_LN2      0.693147180559945309417
#define M_LN10     2.30258509299404568402
#define M_PI       3.14159265358979323846
#define M_PI_2     1.57079632679489661923
#define M_PI_4     0.785398163397448309616
#define M_1_PI     0.318309886183790671538
#define M_2_PI     0.636619772367581343076
#define M_2_SQRTPI 1.12837916709551257390
#define M_SQRT2    1.41421356237309504880
#define M_SQRT1_2  0.707106781186547524401
#endif

#endif
