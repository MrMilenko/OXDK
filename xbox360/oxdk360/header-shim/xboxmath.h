/* Stand-in for the XDK's xboxmath.h, which is a thin layer over xnamath.h.
 *
 * xnamath.h hard-errors without VMX128, which clang does not implement. Projects reach
 * it by more than one route: xtl.h pulls it in through d3dx9.h, and xgraphics.h pulls it
 * in through this header. Both are angle-bracket includes, so putting a stand-in earlier
 * on the include path heads them off; the quoted includes further down the chain cannot
 * be intercepted at all, which is why interception has to happen here rather than deeper.
 *
 * Defining this header's own guard is what stops the real one, since the guard sits above
 * the #error. Code that genuinely computes with XNA math needs the real thing and
 * therefore needs VMX128 in the compiler; this exists for the much larger amount of code
 * that includes xgraphics.h or xtl.h for unrelated declarations.
 */
#ifndef __XBOXMATH2_H__
#define __XBOXMATH2_H__

/* The types, from the xnamath stand-in alongside this file. */
#include <xnamath.h>

#endif /* __XBOXMATH2_H__ */
