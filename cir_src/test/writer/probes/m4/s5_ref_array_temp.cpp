//options: --c++11
// A reference bound to a temporary array built by list initialization.
extern "C" int printf(const char *, ...);
struct B { B() { printf("B\n"); } ~B() { printf("~B\n"); } };
struct A { int v; A() : v(0) { printf("A()\n"); } A(B) : v(1) { printf("A(B)\n"); } ~A() { printf("~A(%d)\n", v); } };
using Arr = A[3];
int main() {
  {
    auto &&ref = Arr{B()};
    printf("in scope %d %d %d\n", ref[0].v, ref[1].v, ref[2].v);
  }
  { const Arr &r2 = Arr{B(), B()}; printf("r2 %d %d\n", r2[0].v, r2[1].v); }
  return 0;
}
