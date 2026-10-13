//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// A catch parameter copy that the library cannot hold in its single-block
// thunk (default arguments or temporaries) and a new with many placement
// operands and an operator delete are diagnosed, and the function is dropped.
struct A { A(); ~A(); };
struct C { C(const C &, const A & = A()); C(); ~C(); };
void g();
void ok() { try { g(); } catch (int) { } }
void bad() { try { g(); } catch (C c) { } }
