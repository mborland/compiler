/* A's key function (the first non-inline virtual function: f) is defined in
   a.cpp, so A's vtable, typeinfo and typeinfo name are emitted there (strong
   symbols) and b.cpp only refers to them.  B's key function is in b.cpp. */
struct A {
  virtual int f();
  virtual int g() { return 20; }
  virtual ~A();
  int a = 1;
};
struct B : A {
  int f() override;
  virtual int h();
  int b = 2;
};
A *make_a();
A *make_b();
int sum_a(A *p);
