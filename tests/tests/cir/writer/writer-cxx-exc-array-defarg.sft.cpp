//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// An array of objects whose constructor has a default argument: the loop
// (ARRAY_CTOR_BEGIN, ARRAY_PARTIAL_DTOR) keeps the element pointer in a local,
// builds the default argument (here with a temporary, in a FULLEXPR in the
// loop body), calls the constructor on the local's value and ends the loop;
// a local array, a member array and new[].
struct A { A(); A(const A &); ~A(); };
struct B { B(const A & = A()); B(const B &); ~B(); };
struct C { C(int = 3); ~C(); };
struct M { B arr[2]; M(); };
M::M() { }
void f1() { B bs[3]; }
void f2() { C cs[2]; }
B *f3() { return new B[4]; }
B *f4(int n) { return new B[n]; }
