extern "C" int printf(const char *, ...);
extern "C" int putchar(int);
int trace;
void note(int x) { trace = trace * 10 + x; putchar('0' + x); }
struct S { int id; S(int i) : id(i) { note(i); } ~S() { note(i2(id)); } static int i2(int v) { return v + 1; } };
struct T { int v; T(int x) : v(x) {} ~T() { putchar('a' + v); } };
int f(int v) { note(v); return v; }
static S sa(1);
namespace N { S sn(3); int nv = f(4); }
struct H { static S sm; static int cnt; };
S H::sm(5);
int H::cnt = f(6);
S arr[2] = { S(7), S(8) };
inline S &inl() { static S s(9); return s; }
T tconst = { 1 };
const T &tref = T(2);
int tmp_init = T(3).v + f(4);
int fn_static(int c) { static int calls = f(10); static T lt(c); calls++; return lt.v + calls; }
thread_local int tl = 5;
int tls_local() { static thread_local int k = f(2); return k + tl; }
int main() {
  putchar('|');
  int r = fn_static(1) + fn_static(2) + inl().id + tls_local() + N::nv + H::cnt;
  printf("| %d\n", r);
  return 0;
}
