//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// M4 slice S1: the sig of CLASS_VFUNCS (two functions have the same sig iff
// the same name, the same parameter types as the front end compares them and
// the same cv-qualifiers of this; ref-qualifiers are ignored) and the order of
// the list: the functions declared in the class in declaration order, the
// implicit ones last in slot order.
struct S {
  virtual void f(int);
  virtual void f(const int &);
  virtual void f(long);
  virtual void f(long long);
  virtual void g() const;
  virtual void g();
  virtual void h() &;
  virtual void h(int) &&;
  virtual operator int();
  virtual operator long();
  virtual void i(int *);
  virtual void i(const int *);
  virtual void j(char);
  virtual void j(signed char);
  virtual ~S();
};
struct T : S {
  void f(int) override;
  void g() const override;
  operator long() override;
  void i(const int *) override;
};
struct U : T {
  void j(char) override;
  virtual void extra();
};
void use(S *s, T *t, U *u) {
  s->f(1L);
  s->g();
  t->g();
  u->extra();
  u->j('a');
  int n = *s;
  (void)n;
}
int main() { return 0; }
