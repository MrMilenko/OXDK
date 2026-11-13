// Over-aligned locals and alloca together: a frame pointer and a base pointer,
// both saved by the prologue, through the large-frame path.
extern void ext(void);
extern void use(void *);
int nv_basepointer(int n)
{
    char __attribute__((aligned(64))) b[64];
    char *p = __builtin_alloca(n);
    use(b);
    use(p);
    ext();
    return b[1] + p[0] + n;
}
