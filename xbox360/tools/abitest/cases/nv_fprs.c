// The nonvolatile FPRs, f14-f31, across a call.
extern void ext(void);
volatile double fin[18];
double nv_fprs(void)
{
    double v[18];
    int i;
    for (i = 0; i < 18; i++) v[i] = fin[i];
    ext();
    double t = 0;
    for (i = 0; i < 18; i++) t = t * 1.5 + v[i];
    return t;
}
