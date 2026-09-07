// A local array must not overlap the return address at caller_sp-8. The audit found
// clang putting b[2] exactly there, so the callee that receives the buffer destroys
// the return address of the function that lent it.
extern void g(int *);
int locals(void) { int b[4]; g(b); return b[0] + b[1] + b[2] + b[3]; }
