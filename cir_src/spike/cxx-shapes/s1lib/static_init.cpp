// The C++ the hand-written ECIL stream test/cirgen/cxx-static-init.test
// encodes.  Run with the pin's clang for the reference output:
//   clang -fclangir static_init.cpp -o a.out && ./a.out
extern "C" int putchar(int);
int g_log[64];
int g_n;
void lg(int v) { g_log[g_n] = v; g_n = g_n + 1; }
int f(int v) { lg(v); return v; }
struct S {
  int id;
  S(int i) { id = i; lg(i); }
  ~S() { putchar(id + 64); }
};
struct Z { int v; ~Z() { putchar(v + 64); } };
Z z = {5};
inline Z iz = {8};
int h4() { static Z lz = {7}; return lz.v; }
int a = f(1);
int b = (f(2) > 0 && f(3) > 0) ? f(4) : f(5);
S s(6);
inline int iv = f(7);
template <class T> struct TV { static T v; };
template <class T> T TV<T>::v = f(8);
int use_tv() { return TV<int>::v; }
const S &sr = S(11);
int h() { static int x = f(9); static S ls(10); return x + ls.id; }
int h2() { static const S &lr = S(12); return lr.id; }
int h3(int b) { static int y = (b && f(1) > 0) ? f(2) : f(3); return y; }
int main() {
  int n0 = g_n;
  int r = use_tv() + iv;
  r += h(); r += h(); r += h2(); r += h3(1); r += h3(0); r += h4(); r += z.v + iz.v;
  for (int i = 0; i < g_n; ++i) putchar('0' + g_log[i] % 10);
  putchar('|');
  putchar('0' + n0 % 10);
  putchar('\n');
  return 0;
}
