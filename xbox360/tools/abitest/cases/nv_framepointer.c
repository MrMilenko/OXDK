// A variable sized array gives the function a frame pointer, r31, which the
// prologue saves itself rather than through the callee-saved list.
extern void ext(void);
extern void use(void *);
int nv_framepointer(int n)
{
    char buf[n];
    use(buf);
    int a = n * 3;
    ext();
    return a + buf[0];
}
