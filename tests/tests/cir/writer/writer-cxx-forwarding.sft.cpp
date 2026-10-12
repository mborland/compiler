//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// A by-value parameter of a class that is not trivially copyable, forwarded by
// an inheriting constructor to the inherited one: the parameter itself is
// passed (no copy), as g++ and clang do; a static invoker forwards it too.
extern "C" int printf(const char *, ...);
struct T {
  int v;
  T(int x) : v(x) { printf("T(%d)\n", v); }
  T(const T &o) : v(o.v + 100) { printf("copy %d\n", v); }
  ~T() { printf("~T(%d)\n", v); }
};
struct B {
  int r;
  B(T t, int k) : r(t.v + k) { printf("B(%d)\n", r); }
  B(int a, T t) : r(a * t.v) { printf("B2(%d)\n", r); }
};
struct D : B { using B::B; };
struct E : D { using D::D; int e = 7; };
T ident(T t) { printf("ident\n"); return t; }
int main_unused() {
  auto lam = [](T t, int k) { printf("lam %d\n", t.v); return t.v + k; };
  int (*fp)(T, int) = lam;
  int r1 = fp(T(1), 2);
  printf("r1 %d\n", r1);
  auto lam2 = [](T t) -> T { return t; };
  T (*fp2)(T) = lam2;
  T t2 = fp2(T(9));
  printf("t2 %d\n", t2.v);
  D d(T(3), 4);
  printf("d %d\n", d.r);
  D d2(5, T(6));
  printf("d2 %d\n", d2.r);
  E e(T(7), 8);
  printf("e %d %d\n", e.r, e.e);
  T tt(2);
  D d3(tt, 1);
  printf("d3 %d\n", d3.r);
  auto lam3 = [](const T &t, T u) { return t.v + u.v; };
  int (*fp3)(const T &, T) = lam3;
  printf("%d\n", fp3(tt, T(10)));
  return 0;
}
