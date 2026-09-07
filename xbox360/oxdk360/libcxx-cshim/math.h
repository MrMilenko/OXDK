// Clean C <math.h> for libc++ mode.
//
// The XDK's own <math.h> adds C++ overloads of its own (abs(double), float inlines,
// hypot) which collide with the ones libc++ declares: <cmath> does `using ::abs` and
// finds two competing sets. This replacement exposes only the C interface and lets
// libc++ layer its C++ overloads on top, exactly as on a normal platform. Defining
// the XDK header's own guard keeps a sibling include from pulling the overloads back.
#ifndef OXDK360_CSHIM_MATH_H
#define OXDK360_CSHIM_MATH_H
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
double __cdecl ceil(double);
double __cdecl cos(double);
double __cdecl cosh(double);
double __cdecl exp(double);
double __cdecl fabs(double);
double __cdecl floor(double);
double __cdecl fmod(double, double);
double __cdecl frexp(double, int *);
double __cdecl ldexp(double, int);
double __cdecl log(double);
double __cdecl log10(double);
double __cdecl modf(double, double *);
double __cdecl pow(double, double);
double __cdecl sin(double);
double __cdecl sinh(double);
double __cdecl sqrt(double);
double __cdecl tan(double);
double __cdecl tanh(double);

// The 360 CRT ships the double routines only. The float ones are declared here and
// defined in oxdk360/builtins/builtins.c, which forwards each to its double form.
float __cdecl acosf(float);
float __cdecl asinf(float);
float __cdecl atanf(float);
float __cdecl atan2f(float, float);
float __cdecl ceilf(float);
float __cdecl cosf(float);
float __cdecl coshf(float);
float __cdecl expf(float);
float __cdecl fabsf(float);
float __cdecl floorf(float);
float __cdecl fmodf(float, float);
float __cdecl frexpf(float, int *);
float __cdecl ldexpf(float, int);
float __cdecl logf(float);
float __cdecl log10f(float);
float __cdecl modff(float, float *);
float __cdecl powf(float, float);
float __cdecl sinf(float);
float __cdecl sinhf(float);
float __cdecl sqrtf(float);
float __cdecl tanf(float);
float __cdecl tanhf(float);

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

// libc++ in its MSVC mode expects the C runtime to supply the C99 classification
// functions as overloads. The XDK CRT does not, so they come from builtins here and
// <cmath>'s `using ::isinf` resolves.
#ifdef __cplusplus
inline bool isinf(float x)          { return __builtin_isinf(x); }
inline bool isinf(double x)         { return __builtin_isinf(x); }
inline bool isinf(long double x)    { return __builtin_isinf(x); }
inline bool isnan(float x)          { return __builtin_isnan(x); }
inline bool isnan(double x)         { return __builtin_isnan(x); }
inline bool isnan(long double x)    { return __builtin_isnan(x); }
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
