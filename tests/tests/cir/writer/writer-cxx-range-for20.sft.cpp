//type:fp
//options:--c++20
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// C++20: the init-statement of a range-based for, a structured binding
// captured by a lambda, a range-based for in a lambda, the lifetime of the
// temporaries of the range expression for the whole loop.
extern "C" int printf(const char *, ...);
struct Res {
  int tag;
  Res(int t) : tag(t) { printf("Res(%d)\n", tag); }
  ~Res() { printf("~Res(%d)\n", tag); }
};
struct Box {
  int d[3];
  Res r;
  Box(int t) : r(t) { d[0] = t; d[1] = t + 1; d[2] = t + 2; }
  const int *begin() const { return d; }
  const int *end() const { return d + 3; }
};
Box mk(int t) { return Box(t); }
struct P { int x, y; };
int main() {
  int a[] = { 1, 2, 3 };
  int i = 0;
  for (int k = 10; int x : a) printf("init %d %d\n", k + x, i++);
  for (i = 5; auto x : a) printf("assign %d %d\n", x, i++);
  for (int s[] = { 4, 4 }; auto x : s) printf("arr %d\n", x);
  for (int x : mk(20)) printf("tmp %d\n", x);
  P p = { 7, 8 };
  auto [u, v] = p;
  auto f = [u, v](int w) { return u * 100 + v * 10 + w; };
  auto g = [=] { return f(u); };
  printf("lambda %d %d\n", f(1), g());
  auto h = [&u, &a](int n) { int t = 0; for (int x : a) t += x * n + u; return t; };
  printf("h %d\n", h(2));
  return 0;
}
