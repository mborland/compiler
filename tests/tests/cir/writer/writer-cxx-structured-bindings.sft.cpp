//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// Structured bindings: members, arrays, bit fields, tuple-like (get<N>),
// references, at namespace scope and in functions; the loop that an implicit
// assignment of an array of class objects builds around a temporary index.
extern "C" int printf(const char *, ...);
namespace std {
template<class T> struct tuple_size;
template<unsigned long N, class T> struct tuple_element;
}
struct Q { int x, y; };
namespace std {
template<> struct tuple_size<Q> { static const unsigned long value = 2; };
template<unsigned long N> struct tuple_element<N, Q> { typedef int type; };
}
template<unsigned long N> int get(Q &&q);
template<> int get<0>(Q &&q) { return q.x * 10; }
template<> int get<1>(Q &&q) { return q.y * 100; }
struct T { int i, j; };
T h = { 5, 6 };
auto [gi, gj] = h;
int gs = gi + gj;
int *gp = &gi;
int bumped() { gi += 1; return gi; }
struct B { unsigned u : 3; unsigned v : 5; };
int f1(Q q) { auto [a, b] = q; return a + b; }
int f2(T &t) { auto &[a, b] = t; a = 30; b += 4; return t.i + t.j; }
int f3(int (&arr)[3]) { auto [a, b, c] = arr; return a + 2 * b + 3 * c; }
int f4(B b) { auto [p, r] = b; return p * 100 + r; }
int f5(const T &t) { const auto &[a, b] = t; return a - b; }
struct A { int i; A &operator=(const A &o) { i = o.i + 1; return *this; } };
struct C { A arr[3]; };
int main() {
  Q q = { 2, 3 };
  T t = { 1, 2 };
  int arr[3] = { 1, 2, 3 };
  B b; b.u = 5; b.v = 17;
  printf("%d %d\n", gi, gs);
  int bv = bumped();
  printf("%d %d\n", bv, *gp);
  printf("%d\n", f1(q));
  int fv = f2(t);
  printf("%d %d %d\n", fv, t.i, t.j);
  printf("%d\n", f3(arr));
  printf("%d\n", f4(b));
  printf("%d\n", f5(t));
  C c1, c2;
  for (int k = 0; k < 3; k++) c1.arr[k].i = k * 10;
  c2 = c1;
  printf("%d %d %d\n", c2.arr[0].i, c2.arr[1].i, c2.arr[2].i);
  return 0;
}
