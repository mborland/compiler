//options:--c++20
// C++17/20: the right operand of an overloaded assignment, and the first
// operand of a reversed rewritten comparison, are evaluated first.
extern "C" int printf(const char *, ...);
struct E { int v; E &operator=(int x) { v = x; return *this; } };
struct R { int v; R &operator=(const int &x) { v = x; return *this; } };
struct Vec {
  E e[4];
  R r[4];
  E &at(int i) { printf("at %d\n", i); return e[i]; }
  R &rat(int i) { printf("rat %d\n", i); return r[i]; }
  E &operator[](int i) { printf("op[] %d\n", i); return e[i]; }
};
int f() { printf("f\n"); return 5; }
int g() { printf("g\n"); return 6; }
int idx() { printf("idx\n"); return 2; }
struct A { int v; };
struct B {
  int v;
  bool operator==(const A &a) const { return v == a.v; }
};
A geta() { printf("geta\n"); return A{ 3 }; }
B getb() { printf("getb\n"); return B{ 3 }; }
int main() {
  Vec v = {};
  int i = 1;
  v.at(i++) = i;
  printf("%d %d %d\n", i, v.e[1].v, v.e[2].v);
  i = 1;
  v.rat(i++) = i;
  printf("%d %d %d\n", i, v.r[1].v, v.r[2].v);
  v[idx()] = f();
  printf("%d\n", v.e[2].v);
  v.at(0) = g();
  E x = {};
  x = f();
  v.e[3] = i;
  printf("%d %d\n", x.v, v.e[3].v);
  bool eq = geta() == getb();
  printf("%d\n", (int)eq);
  return 0;
}
