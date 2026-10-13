//options:--c++17
// (the S1 writer test of the same name in tests/tests/cir/writer is this program)
// M4 slice S1: classes that are named late or are TU-local.  A typedef name
// for linkage comes after the class is first needed, so CLASS_MANGLE is
// re-issued; a class in an anonymous namespace has a TU_LOCAL typeinfo and an
// internal vtable; a class local to a function is described in the frame of
// its function, with the linkage of that function's statics.
extern "C" int printf(const char *, ...);
typedef struct { virtual void f() { printf("TN::f\n"); } } TN;
namespace {
struct Anon { virtual void f() { printf("Anon::f\n"); } int a; };
}
struct Base { virtual void g() { printf("Base::g\n"); } virtual ~Base() {} };
int local() {
  struct Loc : Base { void g() override { printf("Loc::g\n"); } };
  Loc l;
  l.g();
  Base &r = l;
  r.g();
  return 0;
}
inline int in_inline() {
  struct InL : Base { void g() override { printf("InL::g\n"); } };
  InL l;
  l.g();
  return 0;
}
int main() {
  TN tn;
  tn.f();
  Anon an;
  an.f();
  return local() + in_inline();
}
