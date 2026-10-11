//type:rp
//options:--c++17
//require:BACK_END_TYPE 3
// Static storage duration (slice S3): namespace-scope variables with dynamic
// initialization in declaration order, inline variables, template static
// members, function-local statics initialized on first use, lifetime-extended
// temporaries in static references, constant-initialized objects with
// destructors; the destructors print, so the output checks the exit order.
extern "C" int printf(const char *, ...);
extern "C" int putchar(int);
int log_[64];
int n_;
int f(int v) { log_[n_++] = v; return v; }
struct S {
  int id;
  S(int i) : id(i) { f(i); }
  ~S() { putchar('A' + id); }
};
struct Z { int v; ~Z() { putchar('a' + v); } };
Z z = {1};
int a = f(1);
int b = (f(2) > 0 && f(3) > 0) ? f(4) : f(5);
S s(6);
inline int iv = f(7);
template <class T> struct TV { static T v; };
template <class T> T TV<T>::v = f(8);
int use_tv() { return TV<int>::v; }
const S &sr = S(9);
int h() { static int x = f(10); static S ls(11); return x + ls.id; }
int h2() { static const S &lr = S(12); return lr.id; }
int h3(int c) { static int y = (c && f(13) > 0) ? f(14) : f(15); return y; }
int h4() { static Z lz = {3}; return lz.v; }
int main() {
  int n0 = n_;
  int r = use_tv() + iv + z.v;
  r += h(); r += h(); r += h2(); r += h3(1); r += h3(0); r += h4();
  for (int i = 0; i < n_; ++i) printf("%d ", log_[i]);
  printf("| %d %d\n", n0, r);
  return 0;
}
