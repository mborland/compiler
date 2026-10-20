//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// The assignment of a class without data but with a destructor (std::allocator
// is one): nothing is copied, the operands are evaluated (the right one
// first), and no value of the class is read and thrown away: the class is
// NONTRIVIAL_ABI, a record prvalue that is dropped has to be materialized and
// destroyed.  The right operand is only designated, whether it is a variable,
// a member, an element, an indirection, a base-class cast or a call that
// returns a reference.
struct A { ~A() {} };
struct B : A { int x; };
A g_a;
A &ref_a();
const A &cref_a();
void vars() { A a1, a2; a1 = a2; }
void refs(A &a, const A &b) { a = b; }
void globals(A &a) { a = g_a; }
void members() { struct H { A m; } h1, h2; h1.m = h2.m; }
void elements(A (&x)[2], A (&y)[2]) { x[0] = y[1]; }
void pointers(B *t, const B *o) { *static_cast<A *>(t) = *static_cast<const A *>(o); }
void bases(B &t, const B &o) { static_cast<A &>(t) = static_cast<const A &>(o); }
void deriveds(A &a, B &o) { a = static_cast<A &>(o); }
void calls() { ref_a() = cref_a(); }
