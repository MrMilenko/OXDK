// No callee-saved registers and no locals at all. The reservation must not be a side
// effect of the callee-saved area being pushed down, so this path had nothing to
// push and the return address landed in the function's own outgoing shadow area.
extern void q(void);
void nocsr(void) { q(); }
