// S1: covariant return types: a pointer with a this adjustment (the
// _ZTch thunk), a reference, and one without adjustment (no thunk).
struct R1 { virtual ~R1(); int r1; };
struct R2 { virtual ~R2(); int r2; };
struct D : R1, R2 { ~D(); int d; };
R1::~R1() {}
R2::~R2() {}
D::~D() {}
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
};
R2 *A::get() { return 0; }
R1 &A::ref() { return *static_cast<R1 *>(0); }
R1 *A::same() { return 0; }
A::~A() {}
D *B::get() { return 0; }
D &B::ref() { return *static_cast<D *>(0); }
D *B::same() { return 0; }
R2 *call(A *a) { return a->get(); }
