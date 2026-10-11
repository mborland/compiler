//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
typedef struct { int q; int get() const { return q; } } UnnamedT;
UnnamedT ut = { 1 };
int use_ut() { return ut.get(); }
struct { int z; int zz() { return z; } } anon_obj;
int use_anon() { return anon_obj.zz(); }
auto deduced(int a) { return a + 1.5; }
int use_d() { return (int)deduced(1); }
struct Outer { struct { int i; int ii() { return i; } } inner; int f() { return inner.ii(); } };
int use_outer(Outer *o) { return o->f(); }
int locl(int w) { struct L { int w; int get() { return w; } }; L *l = 0; return l->get() + w; }
namespace { int a1(int x) { return x; } struct Priv { int f() { return 1; } }; }
static int s1(int x) { return x; }
int use_priv(Priv *p) { return a1(1) + s1(2) + p->f(); }
