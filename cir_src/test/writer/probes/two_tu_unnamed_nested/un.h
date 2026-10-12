struct A {
  struct {
    int f() { static int s; return ++s; }
    int g() { return 10; }
  } m;
  struct { int x; int get() const { return x; } } n;
};
inline int ia(A &a) { return a.m.f(); }
