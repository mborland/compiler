/* C has two polymorphic bases: its overriders of A2's functions are reached
   through non-virtual thunks (this adjusted by -sizeof(A1 part)), the
   destructor's too. */
struct A1 {
  virtual int f();
  virtual ~A1();
  int a1 = 1;
};
struct A2 {
  virtual int g(int x);
  virtual int h(int x, double y);
  virtual ~A2();
  int a2 = 2;
};
struct C : A1, A2 {
  int f() override;
  int g(int x) override;
  int h(int x, double y) override;
  ~C() override;
  int c = 3;
};
A2 *make_c_as_a2();
int call_a2(A2 *p);
