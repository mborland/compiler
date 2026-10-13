// S1: a class in an anonymous namespace: its vtable, typeinfo and typeinfo
// name have internal linkage (local symbols).
struct Base {
  virtual int f() = 0;
  virtual ~Base() {}
};
namespace {
struct Impl : Base {
  int f() override { return 3; }
  virtual int h();
};
int Impl::h() { return 1; }
}
Base *make() { return new Impl; }
int use() { Impl i; return i.f() + i.h(); }
