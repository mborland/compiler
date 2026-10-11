// Namespace-scope arrays of objects with constructors (STATIC_INIT with
// ARRAY_CTOR and an exit ARRAY_DTOR), CRTP records declared out of order,
// a class conditional initializing a static.
extern "C" int printf(const char *, ...);
int n;
struct A { int id; A() : id(++n) { printf("A() %d\n", id); } ~A() { printf("~A %d\n", id); } };
A one[1];
A arr[3];
A grid[2][2];
namespace N { A pair[2]; }
template <class T> struct S { S() { printf("S\n"); } int i; };
static S<int> s[2];
template <class T> struct W { T *p; W() : p(0) {} };
template <class T> struct E { W<T> w; };
struct B : E<B> { int x; };
B b;
struct C2 { C2(int x) : v(x) { printf("C2(%d)\n", x); } ~C2() { printf("~C2\n"); } int v; };
int sel = 1;
static C2 chosen = sel ? C2(7) : C2(8);
int main() {
  printf("%d %d\n", b.w.p == 0, chosen.v);
  printf("main %d %d\n", arr[2].id, grid[1][1].id);
  return 0;
}
