// Host of cxx-s4-vptr-before-base-init.test: a class derived virtually from C by
// g++ or clang calls the C2 constructor of the CIR unit with the VTT of its
// own vtable group (PR 9629: C2 has to set the vptr before B(this) converts
// `this` to the virtual base A).
struct A { static A *a; A(); };
struct B { static A *a; B(A *); };
struct C : virtual public A, public B { C(); };
struct D : virtual public C {};
struct E : virtual public C { int e; };
extern "C" int cir_main();
int main() {
  new E();
  if (A::a != B::a) return 1;
  new D();
  if (A::a != B::a) return 2;
  return cir_main();
}
