//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
struct S { unsigned v; };
int ext();
const static S s1 = { 1 };
S s2 = { 2 };
const S s3 = { (unsigned)ext() };
int copies() { S a = s1; S b = s2; S c = s3; return a.v + b.v + c.v; }
unsigned ref_cast(int &x) { return (unsigned &)x; }
void ref_cast_store(int &x) { (unsigned &)x = 3; }
int c;
int foo(int);
void pre_lvalue() { --c = foo(1); (foo(2), --c) = foo(1); ++c += 2; }
int values() { int z = ++c; z += --c; ++c; return z + (--c) + (c++); }
int &assign_lvalue(int &a, int &b) { return a = b; }
int assign_chain(int a, int b) { int z = (a = b); return z + (a += b) + ((a = 3) = 4); }
char a1, b1;
char &bar(int i, int j);
void cond_lvalue(int k) { (foo(2) ? bar(3, 0) : bar(3, 1)) = foo(1); (k ? a1 : b1) = 5; }
void f_ref(void (&r)(), void (*p)()) { r(); p(); void (*q)() = r; q(); }
template <class T> void call_ref(const T &fcn) { fcn(); }
void f_ref_target();
void use_call_ref() { call_ref<void()>(f_ref_target); }
int foo1();
int (&foo1_ref)() = foo1;
int default_arg(int i = foo1_ref()) { return i; }
int use_default() { return default_arg(); }
struct A { A(); ~A(); A(const A &); int v; };
void conditional_init(bool k) { const A x = k ? A() : A(); (void)x; }
A (*pa)[3];
struct Z { bool a; int b; };
int class_conditional() { bool k = false; const Z x = k ? Z{true, 1} : Z{false, 0}; return x.a; }
A *block_extern_user() { extern A *pa2; return pa2; }
A *pa2;
