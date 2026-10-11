extern "C" int printf(const char *, ...);
int trace;
void note(int x) { trace = trace * 10 + x; }
struct T { int t; T(int x) : t(x) { note(1); } T(const T &o) : t(o.t) { note(2); } ~T() { note(3); } int get() const { return t; } T inc() const { return T(t + 1); } };
struct W { T a; T b; W(const T &x) : a(x), b(T(x.get() + 10)) { note(4); } ~W() { note(5); } };
struct Base { int b; Base() : b(2) { note(6); } };
struct Der : Base { int d; Der() : Base(), d(3) { note(7); } };
struct Z : Base { int z; Z() : Base(), z() {} };
T pick(bool c, const T &a, const T &b) { return c ? a : b; }
T copy_param(T p) { return p; }
int chain() { return T(1).inc().inc().get(); }
int wtest() { T x(5); W w(x); return w.a.get() + w.b.get(); }
int ptest() { T x(1); T y = pick(true, x, T(2)); T z = copy_param(y); return y.get() + z.get(); }
int dtest() { Der d; Z z; return d.b + d.d + z.b + z.z; }
int main() {
  printf("%d ", chain()); printf("%d\n", trace); trace = 0;
  printf("%d ", wtest()); printf("%d\n", trace); trace = 0;
  printf("%d ", ptest()); printf("%d\n", trace); trace = 0;
  printf("%d ", dtest()); printf("%d\n", trace); trace = 0;
  return 0;
}
