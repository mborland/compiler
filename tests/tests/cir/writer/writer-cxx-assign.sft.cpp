//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
struct B { int i; char c; B() : i(1), c(2) {} };
struct D : B { char d; };
void asg(B *p, B *q) { *p = *q; }
void ini(B *q) { B b = *q; (void)b; }
struct T { int i; char c; };
void asg2(T *p, T *q) { *p = *q; }
B make();
void a1(D &a, B &b) { static_cast<B &>(a) = b; }
void a2(B *p) { *p = make(); }
void a3(B *p, B *q, bool c) { *p = c ? *q : *p; }
void v1();
void v2() { return v1(); }
struct S { int a : 3; int b : 4; int c; };
void tern_lvalue(S &s, bool c) { (c ? s.c : s.c) = 2; }
