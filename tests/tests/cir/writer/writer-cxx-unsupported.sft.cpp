//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
struct D { int x; ~D(); };
struct V { virtual int f(); int x; V(); };
struct VB { int a; };
struct DV : virtual VB { int d; DV(); };
int fine(int n) { return n + 1; }
V::V() : x(1) {}
int V::f() { return x; }
DV::DV() : d(2) {}
int virt_call(V *v) { return v->f(); }
int asm_use(int v) { asm("nop"); return v; }
int asm_use2(int v) { asm("nop"); return v + 1; }
int lam_use(int p) { auto l = [p](int z) { return z + p; }; return l(1); }
int new_use() { int *p = new int(3); int r = *p; delete p; return r; }
int g_dyn = fine(3);
D g_dtor;
int uses_global() { return g_dyn + g_dtor.x; }
struct P { virtual ~P(); int a; };
struct DP : P { int w; DP() : w(1) {} };
struct BF { int a : 3; int b : 4; };
void bit_field_tern(BF &s, bool c) { (c ? s.a : s.b) = 1; }
extern "C" int puts(const char *);
typedef struct { void f() { puts(__func__); } } NamedByTypedef;
void use_named(NamedByTypedef &t) { t.f(); }
struct T2 { int t; T2(int); ~T2(); operator bool() const; };
T2 mkt2(int);
int bad_for(int n) { for (int i = 0; T2 q = mkt2(i); i++) { } return 0; }
