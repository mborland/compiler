// Lifetime-extended array temporaries, explicit specializations and explicit
// instantiation definitions: run and compare with g++.
extern "C" int printf(const char *, ...);
struct D {
  int v;
  D(int x = 0) : v(x) { printf("ctor %d\n", v); }
  D(const D &o) : v(o.v) { printf("copy %d\n", v); }
  ~D() { printf("dtor %d\n", v); }
};
int ext_array() {
  const D (&r)[2] = {D(1), D(2)};
  printf("in scope %d %d\n", r[0].v, r[1].v);
  return r[0].v + r[1].v;
}
struct Pair { D a[2]; };
int ext_member() {
  const Pair &p = Pair{{D(3), D(4)}};
  return p.a[1].v;
}
template <class T> struct X { static T s; static int u; };
template <class T> T X<T>::s = 1;
template <> int X<int>::s = 5;
template <class T> int X<T>::u = 2;
template int X<long>::u;
template <class T> T vt = T(3);
template <> int vt<int> = 7;
template long vt<long>;
inline int iv0 = 4;
int main() {
  int a = ext_array();
  int b = ext_member();
  printf("%d %d %d %d %ld %d %d %ld\n", a, b, X<int>::s, X<double>::s == 1.0,
         (long)X<long>::u, vt<int>, iv0, vt<long>);
  return 0;
}
