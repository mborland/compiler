/* delete through a base pointer calls the deleting destructor (D0) from the
   vtable; each class's destructor writes its name, so the order of the base
   and member destructors is visible. */
struct Tracer {
  const char *name;
  Tracer(const char *n) : name(n) {}
  ~Tracer();
};
struct A {
  virtual ~A();
  Tracer t{"A.member"};
};
struct B : A {
  ~B() override;
  Tracer t{"B.member"};
};
A *make_b();
void destroy(A *p);
