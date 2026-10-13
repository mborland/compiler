//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// A reference bound to a temporary array (list-initialized) is bound to the
// temporary itself, not to a copy of it: the array decays and is cast as the
// front end's adjustments say, on the storage of the temporary.
struct B { B(); ~B(); };
struct A { A(); A(B); ~A(); };
using Arr = A[3];
void f() {
  auto &&r = Arr{B()};
}
