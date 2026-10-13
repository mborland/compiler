//options:--c++20
// (the S1 writer test of the same name in tests/tests/cir/writer is this program)
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
