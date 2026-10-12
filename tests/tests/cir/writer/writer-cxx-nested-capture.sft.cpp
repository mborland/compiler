//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// A nested lambda captures variables that the enclosing lambda captured: by
// copy (a trivially copyable class, one with a copy constructor) and by
// reference; the source is the member of the enclosing closure, read through
// the enclosing lambda's this (gnats20538-lambda-3).
extern "C" int printf(const char *, ...);
struct T { int v; T(int x) : v(x) { printf("T(%d)\n", x); } T(const T &o) : v(o.v + 100) { printf("copy %d\n", v); } ~T() { printf("~T(%d)\n", v); } };
struct A { int z; };
int tests() {
  int x = 10, y = 20;
  A a{1};
  T t(5);
  auto l1 = [=]() -> int {
    auto l2 = [a, &x, t] { return a.z + x + t.v; };
    auto l3 = [&] { return y + a.z + t.v; };
    return l2() * 20 + l3();
  };
  printf("%d\n", l1());
  auto m1 = [=]() mutable {
    auto m2 = [&x, a]() mutable { x += 1; return x + a.z; };
    int r = m2() + m2();
    return r + x;
  };
  int q1 = m1(); int q2 = m1(); printf("%d %d\n", q1, q2);
  auto n1 = [=] { return [=] { return [=] { return x + y + a.z + t.v; }(); }(); };
  printf("%d\n", n1());
  return 0;
}
