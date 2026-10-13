/* Pure virtual functions that are never called while pure: the vtable of
   the abstract A has __cxa_pure_virtual slots, B overrides them all; a pure
   virtual function with a definition is called explicitly (A::f). */
struct A {
  virtual void f() = 0;
  virtual int g() = 0;
  virtual ~A();
};
struct B : A {
  void f() override;
  int g() override;
};
int run(A *p);
