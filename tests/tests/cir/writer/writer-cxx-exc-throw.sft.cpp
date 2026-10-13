//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// throw (spec 12.5): THROW_BEGIN(t) pushes the glvalue of the exception
// object, the initializer consumes it (a store of a value, the this of a
// constructor call, INIT_CONST of a constant), THROW_END(typeinfo, D1 or nil)
// throws; the temporaries of the operand belong to the enclosing FULLEXPR; a
// rethrow is RETHROW. THROW_BEGIN is not a region.
struct A { A(); ~A(); int v; };
struct T { T(const A &); T(const T &); ~T(); int w; };
struct E { int e; };
struct Tr { int a; int b; };
struct Dyn { virtual ~Dyn(); };
void f1() { throw 1; }
void f2() { throw E{2}; }
void f3() { throw T(A()); }
void f4(const E &e) { throw e; }
void f5() { throw "text"; }
void f6(int *p) { throw p; }
void f7(T &t) { throw t; }
void f8() { throw Dyn(); }
void f9() { throw Tr{1, 2}; }
void rethrow() { throw; }
int f10(bool c) { return c ? 5 : throw 7; }
