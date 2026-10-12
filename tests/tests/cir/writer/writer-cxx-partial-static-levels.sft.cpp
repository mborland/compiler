//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// The constant elements that a partial STATIC_INIT skips are those of the
// variable's own aggregate (and of the nested aggregates of its constant): an
// aggregate built elsewhere (a temporary in a call argument, a new, a
// closure, the aggregate of a dynamic element) is initialized completely, so
// its AGG_INIT_BEGIN keeps the ZERO_FILL flag (P{g()} below).
extern "C" int printf(const char *, ...);
int g() { printf("g\n"); return 7; }
struct P { int a; int b; };
int use(P p) { return p.a * 100 + p.b; }
struct S { int k; int v; };
struct P3 { int a, b, c; };
S s1 = { 1, use(P{ 2, g() }) };
S s2 = { 1, use(P{ g() }) };                  // b of the temporary is zero
struct Node { int v; Node *next; int w; };
struct Reg { int k; Node *n; };
int make() { return 5; }
Reg r1 = { 1, new Node{ 0, nullptr, make() } };
S s3 = { 1, [n = 5] { return n; }() };
struct D { int a, b; ~D() { printf("~D\n"); } };
int f() { return 11; }
struct SD { int k; D d; };
SD sd = { 1, { 2, f() } };
struct SDA { int k; D d[2]; };
SDA sda = { 1, { { 3, f() }, { 4, f() } } };
struct M2 { int k; P arr[2]; };
M2 m2 = { 1, { { 2, g() }, { 3, g() } } };
int local(int n) {
  static S ls = { 1, use(P{ n, g() }) };
  static Reg lr = { n, new Node{ 0, nullptr, make() } };
  return ls.v + lr.n->w;
}
// Not split (clang's tiebreak): a function that may exist in several
// translation units (inline, a template instance) has COMDAT statics, which
// every copy initializes the same way: zero data and the full guarded init.
inline int vague(int k) { static P3 sp = { 1, k, 3 }; return sp.a + sp.b + sp.c; }
template <class T> int tvague(T k) { static int arr[3] = { 5, (int)k, 6 }; return arr[0] + arr[1] + arr[2]; }
int main_unused() {
  printf("%d %d\n", s1.k, s1.v);
  printf("%d %d\n", s2.k, s2.v);
  printf("%d %d %d %d\n", r1.k, r1.n->v, r1.n->next == nullptr, r1.n->w);
  printf("%d %d\n", s3.k, s3.v);
  printf("%d %d %d\n", sd.k, sd.d.a, sd.d.b);
  printf("%d %d %d %d %d\n", m2.k, m2.arr[0].a, m2.arr[0].b, m2.arr[1].a, m2.arr[1].b);
  int l1 = local(4);
  int l2 = local(9);
  printf("%d %d\n", l1, l2);
  vague(1); tvague(2); tvague(3L);
  return 0;
}
