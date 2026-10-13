/* A negative case: the constructor of the abstract class A calls a virtual
   function that is pure at that point (the vptr is A's), which reaches
   __cxa_pure_virtual and aborts.  The program is expected to die of
   SIGABRT: expected.rc is -6. */
struct A {
  A();
  virtual void f() = 0;
  void call() { f(); }
};
struct B : A {
  void f() override;
};
