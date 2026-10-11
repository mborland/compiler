//options:--c++20
// How clang's CIRGen accesses and copies a [[no_unique_address]] member.
struct B { int a; char b; B(); };
struct E {};
struct X { [[no_unique_address]] B b; char c; };
struct Y { [[no_unique_address]] E e; int i; };
struct D : B { char c; };
int f(X *p) { return p->b.a; }
void g(X *p, B &q) { p->b = q; }
E *h(Y *p) { return &p->e; }
B *k(D *p) { return p; }
