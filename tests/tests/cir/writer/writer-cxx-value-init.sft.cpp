//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
struct T { T(); int q; ~T(); };
struct S { S() = default; int a; T t; };
struct U { U() = default; int a; int b; };
struct H { S s; U u; int z; H() : s(), u() {} };
void f() { S s{}; S s2 = S(); U u{}; U *p = new U(); (void)s; (void)s2; (void)u; (void)p; }
S mk() { return S(); }
