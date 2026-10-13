/* The overriders are split from the key function.  C's key function (the
   first virtual function declared in it and not inline) is f, defined in
   a.cpp, so a.cpp emits C's vtable, typeinfo and typeinfo name.  C::g, the
   overrider of A2::g, is defined in b.cpp only.  The slot of A2's vptr in
   C's vtable is the non-virtual thunk _ZThn16_N1C1gEi, which must be defined
   by the translation unit that defines C::g (g++ and clang emit the thunk
   with the function it calls), not only by the one with the vtable. */
struct A1 {
  virtual int f();
  int a1 = 1;
};
struct A2 {
  virtual int g(int x);
  int a2 = 2;
};
struct C : A1, A2 {
  int f() override;
  int g(int x) override;
  int c = 3;
};
A2 *make_c();       // a.cpp: new C
A1 *make_c_as_a1(); // a.cpp
int call_g(A2 *p);  // a.cpp: a virtual call through A2 *
int call_f(A1 *p);  // a.cpp
