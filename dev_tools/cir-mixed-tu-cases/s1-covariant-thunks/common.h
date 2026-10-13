/* Covariant returns: B::get returns D * where A::get returns R2 *, and D's
   R2 subobject is not at offset 0, so the thunk adjusts the returned
   pointer (and the this pointer); ref() does the same for a reference;
   same() needs no adjustment. */
struct R1 { virtual ~R1(); int r1 = 11; };
struct R2 { virtual ~R2(); int r2 = 22; };
struct D : R1, R2 { ~D(); int d = 33; };
struct A {
  virtual R2 *get();
  virtual R1 &ref();
  virtual R1 *same();
  virtual ~A();
};
struct B : A {
  D *get() override;
  D &ref() override;
  D *same() override;
  D object;
};
A *make_b();
int through_a(A *p);
