//type:rp
//options:--c++17 --g++ --gnu_version=140000:--c++17 --g++ --gnu_version=140000 -O2
//require:BACK_END_TYPE 3
// SEC-1-2 (M6.1a): a reference result and 64 parameters.  The nonnull mask of
// the function type has no bit for parameter 63 (bit 0 is the result), and an
// over-wide shift used to wrap to bit 0, so p63 was marked nonnull and
// dereferenceable like the reference result and -O2 deleted its null check.
// Both functions must still take the null branch, whichever the optimizer
// does with the call.
extern "C" int printf(const char *, ...);
int g = 7;
__attribute__((noinline)) int &f64(int p0, int p1, int p2, int p3, int p4, int p5, int p6, int p7, int p8, int p9, int p10, int p11, int p12, int p13, int p14, int p15, int p16, int p17, int p18, int p19, int p20, int p21, int p22, int p23, int p24, int p25, int p26, int p27, int p28, int p29, int p30, int p31, int p32, int p33, int p34, int p35, int p36, int p37, int p38, int p39, int p40, int p41, int p42, int p43, int p44, int p45, int p46, int p47, int p48, int p49, int p50, int p51, int p52, int p53, int p54, int p55, int p56, int p57, int p58, int p59, int p60, int p61, int p62, int *p63) {
  if (p63 == nullptr) return g;
  return *p63;
}
__attribute__((noinline)) int &f63(int p0, int p1, int p2, int p3, int p4, int p5, int p6, int p7, int p8, int p9, int p10, int p11, int p12, int p13, int p14, int p15, int p16, int p17, int p18, int p19, int p20, int p21, int p22, int p23, int p24, int p25, int p26, int p27, int p28, int p29, int p30, int p31, int p32, int p33, int p34, int p35, int p36, int p37, int p38, int p39, int p40, int p41, int p42, int p43, int p44, int p45, int p46, int p47, int p48, int p49, int p50, int p51, int p52, int p53, int p54, int p55, int p56, int p57, int p58, int p59, int p60, int p61, int *p62) {
  if (p62 == nullptr) return g;
  return *p62;
}
int main() {
  int x = 1;
  int *volatile np = nullptr;
  int *volatile px = &x;
  int bad = 0;
  if (&f64(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, np) != &g) bad |= 1;
  if (&f64(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, px) != &x) bad |= 2;
  if (&f63(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, np) != &g) bad |= 4;
  if (&f63(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, px) != &x) bad |= 8;
  printf("bad=%d\n", bad);
  return bad;
}
