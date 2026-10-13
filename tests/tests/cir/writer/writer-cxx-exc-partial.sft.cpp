//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// The elements already built are destroyed if a later one throws: an
// aggregate that has members or elements with destructors begins a
// CLEANUP_BEGIN(eh, DEACTIVATABLE) for each after its initialization, through
// a local that holds the aggregate's address, and ends them before
// AGG_INIT_END (an array member's elements in an ARRAY_DTOR loop); an array
// whose elements are all default-constructed has ARRAY_PARTIAL_DTOR after its
// ARRAY_CTOR_BEGIN.
struct A { A(); A(int); ~A(); int v; };
struct B { A a; int b; A c; };
struct N { A arr[2]; A e; };
void f1() { A arr[3]; }
A *f2(int n) { return new A[n]; }
A *f3() { return new A[3]; }
A *f4() { return new A[2]{A(1), A(2)}; }
void f5() { A x[2] = { A(), A() }; }
void f6() { B x = { A(), 1, A() }; }
void f7() { N n = { { A(1), A(2) }, A(3) }; }
void f8() { N ns[2] = { { { 1, 2 }, 3 }, { { 4, 5 }, 6 } }; }
struct P { A a; A b; P(); };
P::P() { }
struct Q { A arr[2]; Q() : arr{ A(1), A(2) } { } };
