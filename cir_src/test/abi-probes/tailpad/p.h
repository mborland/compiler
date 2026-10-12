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
