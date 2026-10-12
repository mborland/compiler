//type:rp
//options:--c++20 --g++ --gnu_version=140000
//require:BACK_END_TYPE 3
// By-value classes that reuse their base's tail padding (dsize < size), passed, returned and
// mixed with other arguments (register and memory classification), checked by value.  The
// two-translation-unit version of this program (cir_src/test/abi-probes, run against g++ and
// clang in both directions by cir_src/scripts/box_abi_probe.sh) is the ABI proof; this single
// translation unit guards the layout and copy side.
extern "C" int printf(const char *, ...);
// By-value records whose derived class reuses the base's tail padding
// (dsize < size).  The base classes have user constructors, so they are not
// POD for the purpose of layout and Itanium reuses their tail padding.
struct B { int a; char c; B(int x, char y) : a(x), c(y) {} };        // 8, dsize 5
struct D : B { char d; D(int x, char y, char z) : B(x, y), d(z) {} };  // 8, d at 5
struct L { long l; char c; L(long x, char y) : l(x), c(y) {} };      // 16, dsize 9
struct LD : L { char d; short e; LD(long x, char y, char z, short w)
                                    : L(x, y), d(z), e(w) {} };      // 16
struct FD { double x; char c; FD(double a, char b) : x(a), c(b) {} };  // 16, dsize 9
struct FDD : FD { char d; FDD(double a, char b, char z) : FD(a, b), d(z) {} };
struct FF { float f; char c; FF(float a, char b) : f(a), c(b) {} };  // 8, dsize 5
struct FFD : FF { char d; FFD(float a, char b, char z) : FF(a, b), d(z) {} };
struct DD : D { char e; DD(int x, char y, char z, char w) : D(x, y, z), e(w) {} };  // 8, e at 6
struct M1 : B { M1(int x, char y) : B(x, y) {} };                    // 8, no members
struct M2 : B { B inner; M2(int x, char y) : B(x, y), inner(x + 1, y + 1) {} };  // 16
struct TT : L { char d[7]; TT(long x, char y) : L(x, y) { for (int i = 0; i < 7; ++i) d[i] = y + i; } }; // 16

struct SB { short s; char c; SB(short x, char y) : s(x), c(y) {} };       // 4, dsize 3
struct SD : SB { char d; SD(short x, char y, char z) : SB(x, y), d(z) {} };  // 4, d at 3
struct XB { long double x; char c; XB(long double a, char b) : x(a), c(b) {} };  // 32, dsize 17
struct XD : XB { char d; XD(long double a, char b, char z) : XB(a, b), d(z) {} };  // 32 (memory)
struct AA { int x[3]; char c; AA(int a, char b) : c(b) { x[0] = a; x[1] = a + 1; x[2] = a + 2; } };  // 16, dsize 13
struct AD : AA { char d[3]; AD(int a, char b) : AA(a, b) { d[0] = 1; d[1] = 2; d[2] = 3; } };  // 16
struct QB { __int128 q; char c; QB(long a, char b) : q(a), c(b) {} };       // 32, dsize 17
struct QD : QB { char d; QD(long a, char b, char z) : QB(a, b), d(z) {} };   // 32 (memory)
struct HB { char a; char b; HB(char x, char y) : a(x), b(y) {} };           // 2
struct HD : HB { char d; HD(char x, char y, char z) : HB(x, y), d(z) {} };   // 3
int sum_b(B v);       B make_b(int, char);       B pass_b(B v);
int sum_d(D v);       D make_d(int, char, char); D pass_d(D v);
int sum_l(L v);       L make_l(long, char);
int sum_ld(LD v);     LD make_ld(long, char, char, short); LD pass_ld(LD v);
int sum_fd(FD v);     FD make_fd(double, char);
int sum_fdd(FDD v);   FDD make_fdd(double, char, char); FDD pass_fdd(FDD v);
int sum_ff(FF v);     FF make_ff(float, char);
int sum_ffd(FFD v);   FFD make_ffd(float, char, char); FFD pass_ffd(FFD v);
int sum_dd(DD v);     DD make_dd(int, char, char, char); DD pass_dd(DD v);
int sum_m1(M1 v);     M1 make_m1(int, char);
int sum_m2(M2 v);     M2 make_m2(int, char);
int sum_tt(TT v);     TT make_tt(long, char);
int sum_sd(SD v);     SD make_sd(short, char, char);  SD pass_sd(SD v);
int sum_xd(XD v);     XD make_xd(long double, char, char);  XD pass_xd(XD v);
int sum_ad(AD v);     AD make_ad(int, char);          AD pass_ad(AD v);
int sum_qd(QD v);     QD make_qd(long, char, char);   QD pass_qd(QD v);
int sum_hd(HD v);     HD make_hd(char, char, char);   HD pass_hd(HD v);
int many(int a, D v, int b, LD w, int c, FDD x, int d, FFD y, int e, DD z, int f);

int sum_b(B v) { return v.a * 100 + v.c; }
B make_b(int a, char c) { return B(a, c); }
B pass_b(B v) { return v; }
int sum_d(D v) { return v.a * 10000 + v.c * 100 + v.d; }
D make_d(int a, char c, char d) { return D(a, c, d); }
D pass_d(D v) { return v; }
int sum_l(L v) { return (int)v.l * 100 + v.c; }
L make_l(long l, char c) { return L(l, c); }
int sum_ld(LD v) { return (int)v.l * 10000 + v.c * 1000 + v.d * 100 + v.e; }
LD make_ld(long l, char c, char d, short e) { return LD(l, c, d, e); }
LD pass_ld(LD v) { return v; }
int sum_fd(FD v) { return (int)v.x * 100 + v.c; }
FD make_fd(double x, char c) { return FD(x, c); }
int sum_fdd(FDD v) { return (int)v.x * 10000 + v.c * 100 + v.d; }
FDD make_fdd(double x, char c, char d) { return FDD(x, c, d); }
FDD pass_fdd(FDD v) { return v; }
int sum_ff(FF v) { return (int)v.f * 100 + v.c; }
FF make_ff(float f, char c) { return FF(f, c); }
int sum_ffd(FFD v) { return (int)v.f * 10000 + v.c * 100 + v.d; }
FFD make_ffd(float f, char c, char d) { return FFD(f, c, d); }
FFD pass_ffd(FFD v) { return v; }
int sum_dd(DD v) { return v.a * 1000000 + v.c * 10000 + v.d * 100 + v.e; }
DD make_dd(int a, char c, char d, char e) { return DD(a, c, d, e); }
DD pass_dd(DD v) { return v; }
int sum_m1(M1 v) { return v.a * 100 + v.c; }
M1 make_m1(int a, char c) { return M1(a, c); }
int sum_m2(M2 v) { return v.a * 10000 + v.c * 100 + v.inner.a * 10 + v.inner.c; }
M2 make_m2(int a, char c) { return M2(a, c); }
int sum_tt(TT v) {
  int s = (int)v.l * 100 + v.c;
  for (int i = 0; i < 7; ++i) s += v.d[i];
  return s;
}
TT make_tt(long l, char c) { return TT(l, c); }
int many(int a, D v, int b, LD w, int c, FDD x, int d, FFD y, int e, DD z,
         int f) {
  return a + v.a + v.c + v.d + b + (int)w.l + w.c + w.d + w.e + c + (int)x.x +
         x.c + x.d + d + (int)y.f + y.c + y.d + e + z.a + z.c + z.d + z.e + f;
}
int sum_sd(SD v) { return v.s * 10000 + v.c * 100 + v.d; }
SD make_sd(short s, char c, char d) { return SD(s, c, d); }
SD pass_sd(SD v) { return v; }
int sum_xd(XD v) { return (int)v.x * 10000 + v.c * 100 + v.d; }
XD make_xd(long double x, char c, char d) { return XD(x, c, d); }
XD pass_xd(XD v) { return v; }
int sum_ad(AD v) { return v.x[0] * 10000 + v.x[1] * 1000 + v.x[2] * 100 + v.c * 10 + v.d[0] + v.d[1] + v.d[2]; }
AD make_ad(int a, char b) { return AD(a, b); }
AD pass_ad(AD v) { return v; }
int sum_qd(QD v) { return (int)v.q * 10000 + v.c * 100 + v.d; }
QD make_qd(long a, char c, char d) { return QD(a, c, d); }
QD pass_qd(QD v) { return v; }
int sum_hd(HD v) { return v.a * 10000 + v.b * 100 + v.d; }
HD make_hd(char a, char b, char c) { return HD(a, b, c); }
HD pass_hd(HD v) { return v; }

static int bad = 0;
static void check(const char *what, long got, long want) {
  if (got != want) { ++bad; printf("MISMATCH %s: got %ld want %ld\n", what, got, want); }
}
#define CHECK(e, w) check(#e, (long)(e), (long)(w))
int main() {
  B b(7, 3); D d(7, 3, 5); L l(9, 4); LD ld(9, 4, 6, 12);
  FD fd(2.0, 4); FDD fdd(2.0, 4, 6); FF ff(2.0f, 4); FFD ffd(2.0f, 4, 6);
  DD dd(7, 3, 5, 9); M1 m1(7, 3); M2 m2(7, 3); TT tt(9, 4);
  CHECK(sum_b(b), 703); CHECK(sum_b(make_b(8, 2)), 802);
  { B r = pass_b(b); CHECK(r.a, 7); CHECK(r.c, 3); }
  CHECK(sum_d(d), 70305); CHECK(sum_d(make_d(8, 2, 1)), 80201);
  { D r = pass_d(d); CHECK(r.a, 7); CHECK(r.c, 3); CHECK(r.d, 5); }
  { D r = make_d(1, 2, 3); CHECK(r.a, 1); CHECK(r.c, 2); CHECK(r.d, 3); }
  CHECK(sum_l(l), 904); CHECK(sum_l(make_l(11, 5)), 1105);
  CHECK(sum_ld(ld), 94000 + 600 + 12); CHECK(sum_ld(make_ld(2, 3, 4, 5)), 20000 + 3000 + 400 + 5);
  { LD r = pass_ld(ld); CHECK(r.l, 9); CHECK(r.c, 4); CHECK(r.d, 6); CHECK(r.e, 12); }
  CHECK(sum_fd(fd), 204); CHECK(sum_fd(make_fd(3.0, 7)), 307);
  CHECK(sum_fdd(fdd), 20000 + 400 + 6); CHECK(sum_fdd(make_fdd(3.0, 7, 2)), 30000 + 700 + 2);
  { FDD r = pass_fdd(fdd); CHECK(r.x, 2); CHECK(r.c, 4); CHECK(r.d, 6); }
  CHECK(sum_ff(ff), 204); CHECK(sum_ff(make_ff(3.0f, 7)), 307);
  CHECK(sum_ffd(ffd), 20000 + 400 + 6); CHECK(sum_ffd(make_ffd(3.0f, 7, 2)), 30000 + 700 + 2);
  { FFD r = pass_ffd(ffd); CHECK(r.f, 2); CHECK(r.c, 4); CHECK(r.d, 6); }
  CHECK(sum_dd(dd), 7000000 + 30000 + 500 + 9);
  CHECK(sum_dd(make_dd(1, 2, 3, 4)), 1000000 + 20000 + 300 + 4);
  { DD r = pass_dd(dd); CHECK(r.a, 7); CHECK(r.c, 3); CHECK(r.d, 5); CHECK(r.e, 9); }
  CHECK(sum_m1(m1), 703); CHECK(sum_m1(make_m1(5, 1)), 501);
  CHECK(sum_m2(m2), 70000 + 300 + 80 + 4); CHECK(sum_m2(make_m2(1, 2)), 10000 + 200 + 20 + 3);
  CHECK(sum_tt(tt), 904 + (4 + 5 + 6 + 7 + 8 + 9 + 10)); CHECK(sum_tt(make_tt(1, 2)), 102 + (2 + 3 + 4 + 5 + 6 + 7 + 8));
  CHECK(many(1, d, 2, ld, 3, fdd, 4, ffd, 5, dd, 6),
        1 + (7 + 3 + 5) + 2 + (9 + 4 + 6 + 12) + 3 + (2 + 4 + 6) + 4 + (2 + 4 + 6) + 5 + (7 + 3 + 5 + 9) + 6);
  { SD a(3, 4, 5); CHECK(sum_sd(a), 30405); CHECK(sum_sd(make_sd(1, 2, 3)), 10203);
    SD r = pass_sd(a); CHECK(r.s, 3); CHECK(r.c, 4); CHECK(r.d, 5); }
  { XD a(3.0L, 4, 5); CHECK(sum_xd(a), 30000 + 405); CHECK(sum_xd(make_xd(1.0L, 2, 3)), 10000 + 203);
    XD r = pass_xd(a); CHECK(r.x, 3); CHECK(r.c, 4); CHECK(r.d, 5); }
  { AD a(10, 4); CHECK(sum_ad(a), 100000 + 11000 + 1200 + 40 + 6); CHECK(sum_ad(make_ad(1, 2)), 10000 + 2000 + 300 + 20 + 6);
    AD r = pass_ad(a); CHECK(r.x[0], 10); CHECK(r.x[2], 12); CHECK(r.c, 4); CHECK(r.d[0], 1); CHECK(r.d[2], 3); }
  { QD a(3, 4, 5); CHECK(sum_qd(a), 30000 + 405); CHECK(sum_qd(make_qd(1, 2, 3)), 10000 + 203);
    QD r = pass_qd(a); CHECK(r.q, 3); CHECK(r.c, 4); CHECK(r.d, 5); }
  { HD a(1, 2, 3); CHECK(sum_hd(a), 10000 + 200 + 3); CHECK(sum_hd(make_hd(4, 5, 6)), 40000 + 500 + 6);
    HD r = pass_hd(a); CHECK(r.a, 1); CHECK(r.b, 2); CHECK(r.d, 3); }
  CHECK(sizeof(SD), 4); CHECK(sizeof(XD), 32); CHECK(sizeof(AD), 16); CHECK(sizeof(QD), 32); CHECK(sizeof(HD), 3);
  printf("tailpad: %s (%d mismatches)\n", bad == 0 ? "OK" : "BAD", bad);
  return bad != 0;
}
