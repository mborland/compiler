//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// A class with virtual base classes is not supported yet (M4 slice S2): its
// record fails, and every use is dropped with that reason; its derived classes
// too.  A polymorphic class without virtual bases is fine.
struct V { int v; };
struct W : virtual V { int w; W(); };
W::W() : w(1) {}
struct X : W { int x; };
struct P { virtual int f() { return 1; } int p; };
int use(W *w, P *p) { return w->w + p->f(); }
int main() { X x; P p; return use(&x, &p); }
