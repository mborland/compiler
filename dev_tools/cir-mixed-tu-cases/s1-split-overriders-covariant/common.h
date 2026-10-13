/* As s1-split-overriders, with a covariant overrider through a
   non-polymorphic multiple-inheritance result: C::get returns D2 * where
   A2::get returns R2 *, and D2's R2 subobject is not at offset 0, so A2's
   slot in C's vtable is a covariant thunk that adjusts this and the returned
   pointer (to D2's R2 subobject).  C::f (the key function) is in a.cpp;
   C::get is in b.cpp only.  There are no virtual destructors. */
struct R1 {
  int a = 11;
};
struct R2 {
  int b = 22;
};
struct D2 : R1, R2 {
  int d = 33;
};
struct A1 {
  virtual int f();
  int a1 = 1;
};
struct A2 {
  virtual R2 *get();
  int a2 = 2;
};
struct C : A1, A2 {
  int f() override;
  D2 *get() override;
  D2 object;
};
A2 *make_c();       // a.cpp: new C
A1 *make_c_as_a1(); // a.cpp
int through_a2(A2 *p);    // a.cpp: p->get()->b
bool subobject_ok(A2 *p); // a.cpp: get() is C's D2 object's R2 subobject
int call_f(A1 *p);        // a.cpp
