//type:fp
//options:--c++20
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// M4 slice S1: a consteval virtual function has no slot and is in neither
// CLASS_VFUNCS nor CLASS_OVERRIDES.
struct A {
  virtual consteval int f() const { return 1; }
  virtual int g() const { return 2; }
  virtual ~A() = default;
};
struct B : A {
  consteval int f() const override { return 3; }
  int g() const override { return 4; }
};
int use(const A *a) { return a->g(); }
int main() { B b; return use(&b) - 4; }
