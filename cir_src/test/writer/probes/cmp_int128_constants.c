/* 128-bit integer constants must keep all their bits (expressions, static
   initializers, case labels, comparisons). */
int printf(const char *, ...);
typedef unsigned __int128 U;
typedef __int128 S;
volatile U big;
volatile S sbig;
static const U k1 = ((U)1 << 127) + 12345;
static const S k2 = -(((S)1 << 100) + 7);
U arr[3] = { (U)-1, ((U)0x0123456789abcdefULL << 64) | 0xfedcba9876543210ULL, 5 };
struct { S s; U u; int i; } rec = { (S)-2, (U)1 << 90, 7 };
static void show(const char *n, U v) {
  printf("%s %016llx%016llx\n", n, (unsigned long long)(v >> 64),
         (unsigned long long)v);
}
static int sw(S v) {
  switch (v) {
    case 0: return 0;
    case ((S)1 << 70): return 1;
    case -((S)1 << 70): return 2;
    default: return 3;
  }
}
int main(void) {
  big = (U)-1;
  show("allones", big);
  show("k1", k1);
  show("k2", (U)k2);
  show("a0", arr[0]);
  show("a1", arr[1]);
  show("a2", arr[2]);
  show("rec.s", (U)rec.s);
  show("rec.u", rec.u);
  sbig = (S)1 << 70;
  printf("%d %d %d %d\n", sw(sbig), sw(-sbig), sw(0), sw(5));
  big = ((U)1 << 127) + 12345;
  show("expr", big);
  big = (U)0xffffffffffffffffULL + 1;
  show("carry", big);
  printf("%d\n", (int)(big == ((U)1 << 64)));
  big = __builtin_bswap128(((U)0x0102030405060708ULL << 64) | 0x090a0b0c0d0e0f10ULL);
  show("bswap", big);
  return 0;
}
