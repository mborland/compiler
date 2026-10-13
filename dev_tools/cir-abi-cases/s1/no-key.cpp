// S1: no key function (every virtual function is inline): the vtable,
// typeinfo and typeinfo name are emitted in every translation unit that
// needs them, as weak symbols in COMDAT groups.
struct A {
  virtual int f() { return 1; }
  virtual ~A() {}
};
struct B : A {
  int f() override { return 2; }
};
int use(A *a) { return a->f(); }
B b_object;
