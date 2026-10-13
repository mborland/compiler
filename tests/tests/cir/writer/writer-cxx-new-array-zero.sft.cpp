//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// new T[n]() and new T[n]{} with a run-time count value-initialize every
// element by a loop of INIT_ZERO: inside an ARRAY_CTOR_BEGIN/END pair (the
// element pointer and the count) for a record element, in a WHILE loop over a
// counter and a local pointer for another (cir.array.ctor takes only records).
// A plain element, an element with a pointer to a data member (null is -1:
// INIT_ZERO writes it), a multi-dimensional array, and a class with a
// constructor that the class does not provide (zeroed first, then
// constructed). new S[n]{S{...}} (listed elements with a
// run-time count, which needs bad_array_new_length) is still unsupported.
struct B { int a; };
struct S { long a; long B::*pm; long b; };
struct N { N(); int v; };
struct Z { int z; N n; };
int *p1(int n) { return new int[n](); }
S *p2(int n) { return new S[n](); }
S *p3(int n) { return new S[n]{}; }
S (*p4(int n))[3] { return new S[n][3](); }
Z *p5(int n) { return new Z[n](); }
S *bad(int n) { return new S[n]{S{1, nullptr, 2}}; }
