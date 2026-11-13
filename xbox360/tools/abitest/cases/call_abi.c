// Returns and arguments that Microsoft's compiler passes differently from SysV.
// callcheck.py calls each of these from, and has each call into, hand-written
// code that does what Microsoft's code does (xsdk's measurements on hardware):
//
//   - a 64 bit result, and an aggregate of 1, 2, 4 or 8 bytes (one holding a
//     single double included), comes back in r3, eight bytes as one 64 bit value
//   - an aggregate of up to eight bytes is passed as its bytes in one general
//     register: 1, 2 and 4 bytes right justified, other sizes left justified
//   - a char or short argument arrives unextended; the callee extends it

typedef struct { int a, b; } s8;
typedef struct { double d; } sd;
typedef struct { short a; } s2;
typedef struct { char a, b, c; } s3;

long long ret_i64(void) { return 0x1122334455667788LL; }
s8 ret_s8(void) { s8 s = {0x11223344, 0x55667788}; return s; }
sd ret_sd(void) { sd s = {1.5}; return s; }
s2 ret_s2(void) { s2 s = {0x1234}; return s; }

int take_sc(signed char c) { return c; }
int take_uc(unsigned char c) { return c; }
int take_ss(short c) { return c; }
int take_us(unsigned short c) { return c; }
int take_s8(s8 s) { return s.a - s.b; }
int take_s3(int pad, s3 s) { return pad + s.a + s.b + s.c; }

extern long long msvc_ret64(void);
extern s8 msvc_ret8(void);
extern int msvc_take8(s8 s);
extern int msvc_take3(int pad, s3 s);

int call_ret64_lo(void) { return (int)msvc_ret64(); }
int call_ret64_hi(void) { return (int)(msvc_ret64() >> 32); }
int call_ret8(void) { s8 s = msvc_ret8(); return s.a * 256 + s.b; }
int call_take8(void) { s8 s = {1, 2}; return msvc_take8(s); }
int call_take3(void) { s3 s = {1, 2, 3}; return msvc_take3(7, s); }
