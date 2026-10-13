//options: --c++11 --g++ --gnu_version=140000
// Default member initializers of members of anonymous structs and unions, in
// one group per holder.
extern "C" int printf(const char *, ...);
struct A { int v; A() : v(7) { } A(const A &o) : v(o.v + 1) { } };
struct B {
  struct { int m_1 = 1; int m_2 = 2; };
  union { int u1; char u2 = 'x'; };
  int z = 3;
  A dummy;
  B() { }
  B(int q) : z(q) { }
};
struct C {
  struct { long a = 10; int b = 11; };
  int d;
  C() : d(12) { }
};
B g;
int main() {
  B b;
  B c(9);
  B d = b;
  C e;
  printf("%d %d %c %d %d\n", b.m_1, b.m_2, b.u2, b.z, b.dummy.v);
  printf("%d %d %c %d %d\n", c.m_1, c.m_2, c.u2, c.z, c.dummy.v);
  printf("%d %d %c %d %d\n", d.m_1, d.m_2, d.u2, d.z, d.dummy.v);
  printf("%d %d %c %d\n", g.m_1, g.m_2, g.u2, g.z);
  printf("%ld %d %d\n", e.a, e.b, e.d);
  return 0;
}
