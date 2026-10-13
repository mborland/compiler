//options:--c++17
// (the S1 writer test of the same name in tests/tests/cir/writer is this program)
// M4 slice S1: virtual functions in class templates.  An implicit instance
// has no key function (linkonce_odr vtable and typeinfo, COMDAT) and gets its
// virtual members instantiated with its vtable; an explicit instantiation is
// weak_odr; a derived class of an instance has the instance as its base.
extern "C" int printf(const char *, ...);
template <class T> struct Tm {
  T v;
  virtual void f() { printf("Tm::f\n"); }
  virtual T get() const { return v; }
  virtual ~Tm() {}
};
template struct Tm<long>;
struct Der : Tm<int> {
  void f() override { printf("Der::f\n"); }
};
int main() {
  Tm<char> tc;
  Tm<char> &r = tc;
  r.f();
  Der d;
  Tm<int> *p = &d;
  p->f();
  return p->get() + tc.get();
}
