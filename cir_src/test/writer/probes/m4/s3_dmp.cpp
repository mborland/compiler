// S3 probe: the null data-member pointer is -1; every kind of
// zero-initialization of a type that contains one (static storage, S s{},
// new S(), arrays, bases, members, the zero part of a constructor, new[] with
// a constant count) must produce it. Run against g++, except for a pointer in
// a base class of a static or value-initialized derived object, where g++
// writes 0 (its bug; the standard, clang and the writer give -1): those are in
// the golden writer-cxx-dmp-zero-init only.
#include <cstdio>
#include <cstdlib>
struct B { int a; int b; };
struct S { int x; int B::*p; };
struct W { S s; int y; };
struct D : S { int z; };
struct M { int B::*arr[2]; };
struct F { void (B::*f)(); int k; };
struct C { C(); S s; int k; };
C::C() : k(9) {}
struct E : D { E() = default; int q; };
struct Dyn { virtual ~Dyn() {} int B::*p; int n; };
S g; S g2 = { 1, 0 }; S g3 = { 2, &B::b };
W gw; D gd; M gm; F gf; int B::*gp;
static int nul(int B::*p) { return p == nullptr; }
struct NZ { NZ() {} int B::*q; int z; };
struct ZZ { int z; C c; int B::*r; };
int main() {
  volatile int cnt = 3;
  { S *p = new S[cnt](); std::printf("%d %d %d\n", nul(p[0].p), nul(p[2].p), p[1].x); delete[] p; }
  { S *p = new S[cnt]{}; std::printf("%d %d\n", nul(p[1].p), p[2].x); delete[] p; }
  { int *p = new int[cnt](); std::printf("%d %d\n", p[0], p[2]); delete[] p; }
  { S (*p)[2] = new S[cnt][2](); std::printf("%d %d\n", nul(p[2][1].p), p[0][0].x); delete[] p; }
  std::printf("%d %d %d %d\n", nul(g.p), nul(g2.p), g3.p == &B::b, nul(gw.s.p));
  std::printf("%d %d %d %d\n", nul(gm.arr[0]), nul(gm.arr[1]), gf.f == nullptr, nul(gp));
  { S s{}; std::printf("%d %d\n", s.x, nul(s.p)); }
  { S s = {1}; std::printf("%d %d\n", s.x, nul(s.p)); }
  { S *p = new S(); std::printf("%d %d\n", p->x, nul(p->p)); delete p; }
  { S arr[2] = {}; std::printf("%d %d\n", nul(arr[0].p), nul(arr[1].p)); }
  { W w{}; std::printf("%d %d %d\n", nul(w.s.p), w.y, w.s.x); }
  { F f{}; std::printf("%d %d\n", f.f == nullptr, f.k); }
  { static S ls; std::printf("%d\n", nul(ls.p)); }
  { D d{}; std::printf("%d %d\n", d.z, d.x); }
  { E e{}; std::printf("%d\n", e.q); }
  { C c; std::printf("%d\n", c.k); }
  { W *p = new W[3](); std::printf("%d %d %d\n", nul(p[0].s.p), nul(p[2].s.p), p[1].y); delete[] p; }
  { M m{}; std::printf("%d %d\n", nul(m.arr[0]), nul(m.arr[1])); }
  { Dyn *d = new Dyn(); std::printf("%d %d\n", nul(d->p), d->n); delete d; }
  { S *p = (S *)std::malloc(sizeof(S)); *p = S(); std::printf("%d\n", nul(p->p)); std::free(p); }
  return 0;
}
