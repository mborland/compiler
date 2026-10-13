// S1: the key function is not defined here: neither the vtable nor the
// typeinfo of A is emitted (undefined references), but the derived class
// B has its own key function, so B's vtable, typeinfo and name are emitted
// and they reference A's.
struct A {
  virtual void f();
  virtual void g();
};
struct B : A {
  void f() override;
  virtual void h();
};
void B::f() {}
void B::h() {}
A *make() { return new B; }
