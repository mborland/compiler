//type:rp
//options:--gcc --gnu_version=150000:--gcc --gnu_version=150000 -O2
//require:BACK_END_TYPE 3
/* __builtin_clz, ctz, clrsb in their int, long and long long forms, and the
   type-generic __builtin_clzg and __builtin_ctzg (GCC 14) with the constant
   second argument that the writer maps.  Values go through a volatile so that
   the optimizer cannot fold them at compile time. */
static volatile unsigned int v32 = 0x00F00000u;
static volatile unsigned long v64 = 0x0000000100000000ul;
static volatile unsigned long long vll = 0x8000000000000000ull;
static volatile unsigned short v16 = 0x0100;
static volatile unsigned char v8 = 0x10;
static volatile int sign_pos = 0x00ffffff;
static volatile int sign_neg = -2;
static volatile unsigned int zero;

int main(void)
{
  int failures = 0;

  if (__builtin_clz(v32) != 8 || __builtin_ctz(v32) != 20)
    failures |= 1;
  if (__builtin_clzl(v64) != 31 || __builtin_ctzl(v64) != 32)
    failures |= 2;
  if (__builtin_clzll(vll) != 0 || __builtin_ctzll(vll) != 63)
    failures |= 4;
  if (__builtin_clrsb(sign_pos) != 7 || __builtin_clrsb(sign_neg) != 30)
    failures |= 8;
  if (__builtin_clrsbl(-1L) != 63 || __builtin_clrsbll(1LL) != 62)
    failures |= 16;
  if (__builtin_clzg(v16) != 7 || __builtin_ctzg(v16) != 8)
    failures |= 32;
  if (__builtin_clzg(v8) != 3 || __builtin_ctzg(v8) != 4)
    failures |= 64;
  if (__builtin_clzg(v64) != 31 || __builtin_ctzg(vll) != 63)
    failures |= 128;
  /* The two-argument forms give the second argument for a zero value. */
  if (__builtin_clzg(zero, -1) != -1 || __builtin_ctzg(zero, 99) != 99)
    failures |= 256;
  if (__builtin_clzg(v32, -1) != 8 || __builtin_ctzg(v32, 99) != 20)
    failures |= 512;
  return failures & 255 ? failures & 255 : failures >> 8;
}
