// Every nonvolatile GPR in use across a call. Microsoft's code keeps all 64 bits
// of r14-r31 across a call into ours, so each must come back whole, not just its
// low word.
extern void ext(void);
volatile int in[18];
int nv_gprs(void)
{
    int a = in[0], b = in[1], c = in[2], d = in[3], e = in[4], f = in[5];
    int g = in[6], h = in[7], i = in[8], j = in[9], k = in[10], l = in[11];
    int m = in[12], n = in[13], o = in[14], p = in[15], q = in[16], r = in[17];
    ext();
    return a + b * 2 + c * 3 + d * 4 + e * 5 + f * 6 + g * 7 + h * 8 + i * 9 +
           j * 10 + k * 11 + l * 12 + m * 13 + n * 14 + o * 15 + p * 16 +
           q * 17 + r * 18;
}
