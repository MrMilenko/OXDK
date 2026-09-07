/* Stand-in for the XDK's d3dx9.h, for C translation units.
 *
 * xtl.h pulls in d3dx9.h, which pulls in d3dx9math.h, which pulls in xboxmath.h and
 * xnamath.h. Those need VMX128, and their scalar fallback (_XM_NO_INTRINSICS_) is
 * written in C++: d3dx9math.inl uses reinterpret_cast inside exactly that branch,
 * because Microsoft only ever compiled the fallback for x86, where the XDK requires
 * C++ anyway. So a C file that includes xtl.h cannot reach the real header either way.
 *
 * xtl.h includes it with angle brackets, unlike the quoted includes further down the
 * chain, so putting this earlier on the include path is enough to head it off.
 *
 * C++ translation units that genuinely use D3DX or XNA math should include the real
 * header instead, with _XM_NO_INTRINSICS_ defined; see oxdk360.mk.
 */
#ifndef __D3DX9_H__
#define __D3DX9_H__

#include <d3d9.h>

/* The real d3dx9.h declares COM interfaces and so reaches xobjbase.h for the interface
   macros. Consumers rely on that: the XDK's own xuirender.h uses DECLARE_INTERFACE and
   STDMETHOD with no includes of its own, on the understanding that including xtl.h first
   has already established them. Skipping the math does not mean skipping that. */
#include <xobjbase.h>

/* The math header is the only C++-only part of D3DX: d3dx9math.h ends with an
   unconditional #include of d3dx9math.inl, which uses reinterpret_cast. Everything else --
   ID3DXBuffer, the texture loaders, the shader compiler, is plain COM that a C file can
   use. So block the math header by its own guard, declare the four types its non-C++ branch
   would have declared (matching it exactly, so C and C++ objects agree on layout), and let
   the rest through. Without this a C file cannot name LPD3DXBUFFER or PALETTEENTRY. */
#define __D3DX9MATH_H__

typedef struct D3DXVECTOR2 { FLOAT x, y; } D3DXVECTOR2, *LPD3DXVECTOR2;
typedef struct _D3DVECTOR D3DXVECTOR3, *LPD3DXVECTOR3;
typedef struct D3DXVECTOR4 { FLOAT x, y, z, w; } D3DXVECTOR4, *LPD3DXVECTOR4;
typedef struct _D3DMATRIX D3DXMATRIX, *LPD3DXMATRIX;

#include <d3dx9core.h>
#include <d3dx9tex.h>
#include <d3dx9shader.h>

/* The one part of D3DX that is a macro rather than a call into d3dx9.lib, and the only
   part a C file is likely to want. */
#ifndef D3DX_PI
#define D3DX_PI                 ((FLOAT) 3.141592654f)
#endif
#define D3DXToRadian(degree)    ((degree) * (D3DX_PI / 180.0f))
#define D3DXToDegree(radian)    ((radian) * (180.0f / D3DX_PI))

#endif /* __D3DX9_H__ */
