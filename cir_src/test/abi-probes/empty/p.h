// Empty classes passed and returned by value, alone and as members and bases.
struct E {};
struct E2 : E {};
struct E3 : E2 {};
struct SE { E e; int x; SE(int v) : x(v) {} };                  // 8
struct ES { int x; E e; ES(int v) : x(v) {} };                   // 8
struct SEE { E a; E b; SEE() {} };                              // 2
struct DE : E { int x; DE(int v) : x(v) {} };                   // 4
struct EL { E e; long l; EL(long v) : l(v) {} };                // 16
struct EF { E e; double f; EF(double v) : f(v) {} };            // 16
struct EA { E e[3]; };                                          // 3

struct EBE : E { E e; int x; EBE(int v) : x(v) {} };              // base E at 0, member e at 1
struct ZB : E, E3 { int x; ZB(int v) : x(v) {} };                  // E3 shares E's address: pushed to 1
struct NE2 { [[no_unique_address]] E a; [[no_unique_address]] E b; [[no_unique_address]] E c; long x; NE2(long v) : x(v) {} };
long layout_item(int index);       // sizeof/offsetof facts, computed by the TU that defines them
int layout_count();
int f_ebe(EBE s);      EBE make_ebe(int);
int f_zb(ZB s);        ZB make_zb(int);
int f_ne2(NE2 s);      NE2 make_ne2(long);
int f_e(E e, int x);
int f_e_mid(int a, E e, int b);
int f_e2(E2 e, E3 e3, int x);
int f_many(int a, int b, int c, int d, int e, E x, int f, E y, int g, int h);
int f_many_d(double a, double b, double c, double d, double e, double f,
             double g, E x, double h, double i);
E make_e();
E2 make_e2();
E pass_e(E e);
int f_se(SE s);       SE make_se(int);
int f_es(ES s);       ES make_es(int);
int f_see(SEE s, int x);  SEE make_see();
int f_de(DE s);       DE make_de(int);
int f_el(EL s);       EL make_el(long);
int f_ef(EF s);       EF make_ef(double);
int f_ea(EA s, int x);
int f_mix(int a, E e, SE s, ES t, DE u, int b);
