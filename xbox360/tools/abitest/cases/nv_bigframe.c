// A frame too large for a 16 bit offset, so saves go through an index register.
extern void ext(void);
int nv_bigframe(int n)
{
    volatile char b[70000];
    b[n] = 1;
    ext();
    return b[5] + n;
}
