//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
struct S {
  int x;
  S() : x(0) { struct L { L() {} ~L() {} int get() { return 1; } } l; x = l.get(); }
  ~S() { struct E { E() {} int g() { return 2; } } e; x = e.g(); }
};
void use() { S s; }
struct NoBase { int x; };
struct Z : NoBase { int z; Z() : NoBase(), z() {} };
