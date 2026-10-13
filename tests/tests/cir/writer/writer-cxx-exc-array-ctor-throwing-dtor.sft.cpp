//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// A throwing destructor of a temporary of the default argument of an array
// element's constructor: the element whose full expression threw is not
// destroyed, as in clang (g++ destroys it); the earlier elements are.
struct A {
  A() { }
  A(const A &);
  ~A() noexcept(false);
};
struct B {
  B(const A & = A());
  B(const B &);
  ~B();
};
void f() {
  B bs[3];
}
