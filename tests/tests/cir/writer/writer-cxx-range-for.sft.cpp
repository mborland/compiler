//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// Range-based for: arrays, classes with begin/end members, free begin/end,
// temporaries of the range expression, structured bindings, break/continue.
extern "C" int printf(const char *, ...);
struct Res {
  int tag;
  Res(int t) : tag(t) { printf("Res(%d)\n", tag); }
  ~Res() { printf("~Res(%d)\n", tag); }
};
struct It {
  int *p;
  Res r;
  It(int *q) : p(q), r(100) {}
  It(const It &o) : p(o.p), r(o.r.tag + 1) {}
  ~It() { printf("~It\n"); }
  bool operator!=(const It &o) const { return p != o.p; }
  It &operator++() { ++p; return *this; }
  int &operator*() const { return *p; }
};
struct Vec {
  int d[4];
  Res guard;
  Vec() : guard(7) { for (int i = 0; i < 4; i++) d[i] = i + 1; }
  int *begin() { return d; }
  int *end() { return d + 4; }
};
struct Itv {
  Vec &v;
  It begin() { return It(v.d); }
  It end() { return It(v.d + 4); }
};
struct Free { int a[3]; };
int *begin(Free &f) { return f.a; }
int *end(Free &f) { return f.a + 3; }
Vec make() { return Vec(); }
struct P { int x, y; };
struct Obj {
  int v;
  Obj(int x) : v(x) {}
  ~Obj() { printf("~Obj(%d)\n", v); }
};
int main() {
  int arr[5] = { 1, 2, 3, 4, 5 };
  int t = 0;
  for (int x : arr) t += x;
  printf("arr %d\n", t);
  for (int &x : arr) x *= 2;
  for (const auto &x : arr) printf("%d ", x);
  printf("\n");
  Vec v;
  t = 0;
  for (int x : v) t += x;
  printf("vec %d\n", t);
  for (int x : make()) printf("tmp %d\n", x);
  Itv iv = { v };
  for (int x : iv) { if (x == 2) continue; if (x == 4) break; printf("it %d\n", x); }
  Free f = { { 5, 6, 7 } };
  for (auto x : f) printf("free %d\n", x);
  P ps[2] = { { 1, 2 }, { 3, 4 } };
  for (auto [a, b] : ps) printf("sb %d %d\n", a, b);
  for (auto &[a, b] : ps) { a += 10; b += 20; }
  for (const auto &[a, b] : ps) printf("sbr %d %d\n", a, b);
  int m[2][2] = { { 1, 2 }, { 3, 4 } };
  for (auto &row : m) for (int c : row) printf("m %d\n", c);
  return 0;
}
