//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// C++17 evaluates the right operand of a class assignment first.  Its object
// is read when the (trivial) assignment is called, after the target is
// designated, so the writer keeps its address; the value of a call is fixed
// when it returns.  A conditional, a comma expression and the result of an
// assignment are neither, and a class assignment nested in the right operand
// of another would need a second suppressed load: each function is dropped
// with its reason (the same program is miscompiled otherwise).  The last
// function checks that the state of a diagnosed assignment does not leak into
// the next function.
struct T { int a, b; };
T store[4];
T g_a = { 1, 2 }, g_b = { 3, 4 }, obj = { 5, 6 }, t1 = { 20, 21 }, t2 = { 30, 31 };
T *getp() { g_a.a += 10; g_b.a += 10; return &store[1]; }
void cond(bool c) { *getp() = c ? g_a : g_b; }
T *getp2() { obj.a = 77; return &store[2]; }
void comma() { *getp2() = (g_a.b++, obj); }
T *getp3() { t1.a = 50; t2.a = 60; return &store[3]; }
void nested_result() { *getp3() = (t1 = t2); }
T arr[2];
void nested_subscript() { *getp3() = arr[(*getp2() = obj, 0)]; }
void after_a_dropped_function() { *getp2() = obj; }
