/* Stub for the XDK's xnamath.h.
 *
 * The real header hard-errors without VMX128 compiler support, which is Xenon's vector
 * ISA and something clang does not implement. xtl.h pulls it in unconditionally through
 * d3dx9.h, so any project that includes xtl.h stops there.
 *
 * Code that actually uses XMVECTOR and friends needs the real thing, and therefore needs
 * VMX128 in the compiler. This shim exists for the large amount of code that includes
 * xtl.h for windows and D3D types and never touches XNA math. Ports carrying their own
 * vector types, which is most of them.
 */
#ifndef __XNAMATH_H__
#define __XNAMATH_H__

/* Enough for headers that mention the types without using them. Deliberately not a
   working implementation: anything that computes with these should fail to build rather
   than silently compute nonsense. */
/* Pulled in directly rather than relied upon: this header has to work when it is force
   included ahead of everything else, which is how a project reaches the real xnamath.h
   through xtl.h -> d3dx9.h -> d3dx9math.h -> xboxmath.h. Those are quoted includes, so
   they resolve in the XDK's own directory and no -I order can shim them; defining the
   real header's guard first is the only way to head it off. */
#include <vectorintrinsics.h>

typedef __vector4 XMVECTOR;   /* the real intrinsic type, from vectorintrinsics.h */
typedef struct _XMFLOAT2 { float x, y; } XMFLOAT2;
typedef struct _XMFLOAT3 { float x, y, z; } XMFLOAT3;
typedef struct _XMFLOAT4 { float x, y, z, w; } XMFLOAT4;
typedef struct _XMMATRIX { XMVECTOR r[4]; } XMMATRIX;

/* xboxmath.h typedefs its own aligned aliases from these, so they all have to exist even
   though nothing here can compute with them. */
typedef XMFLOAT2 XMFLOAT2A;
typedef XMFLOAT3 XMFLOAT3A;
typedef XMFLOAT4 XMFLOAT4A;
typedef struct _XMFLOAT4X3 { float m[4][3]; } XMFLOAT4X3, XMFLOAT4X3A;
typedef struct _XMFLOAT4X4 { float m[4][4]; } XMFLOAT4X4, XMFLOAT4X4A;
typedef struct _XMVECTORU32 { unsigned int u[4]; } XMVECTORU32;


/* The 16-bit float the XDK's own headers pass around; d3dx9math.h and xgraphics.h both
   declare functions taking it, so it has to exist even though nothing here converts to
   or from it. */
#ifndef _HALF_DEFINED
#define _HALF_DEFINED
typedef unsigned short HALF;
#endif

#ifndef XMFINLINE
#define XMFINLINE __inline
#endif

#endif /* __XNAMATH_H__ */
