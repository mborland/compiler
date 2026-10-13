//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// dynamic_cast: DYNAMIC_CAST (typeinfo of the operand's class, of the target
// class or nil for void *, the result type, REF for a reference that throws
// bad_cast); a reference cast takes the address of its operand and
// dereferences the result. Both classes have their layout (and vtable) facts;
// downcasts, cross casts and casts through virtual bases.
struct A { virtual ~A(); int a; };
struct B : A { int b; };
struct C { virtual ~C(); int c; };
struct D : B, C { int d; };
struct V { virtual ~V(); };
struct X : virtual V { int x; };
struct Y : virtual V { int y; };
struct Z : X, Y { int z; };
B *g1(A *p) { return dynamic_cast<B *>(p); }
const B *g2(const A *p) { return dynamic_cast<const B *>(p); }
B &g3(A &p) { return dynamic_cast<B &>(p); }
void *g4(A *p) { return dynamic_cast<void *>(p); }
const volatile void *g5(const volatile A *p) { return dynamic_cast<const volatile void *>(p); }
C *g6(A *p) { return dynamic_cast<C *>(p); }
D *g7(B *p) { return dynamic_cast<D *>(p); }
A *g8(D *p) { return dynamic_cast<A *>(p); }
Z *g9(V *p) { return dynamic_cast<Z *>(p); }
X *g10(Y *p) { return dynamic_cast<X *>(p); }
C &g11(B &p) { return dynamic_cast<C &>(p); }
bool g12(A *p) { return dynamic_cast<B *>(p) != 0; }
int g13(A *p) { if (B *b = dynamic_cast<B *>(p)) return b->b; return 0; }
