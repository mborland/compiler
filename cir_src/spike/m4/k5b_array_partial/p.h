extern "C" int printf(const char *, ...);
// Partial destruction when an element constructor throws (cir.array.ctor):
// the constructed prefix is destroyed in reverse order.
struct T { int id; static int n; T() : id(++n) { printf("T() %d\n", id); if (id == 3) throw id; } T(int) : id(100) { printf("T(int)\n"); } ~T() { printf("~T() %d\n", id); } };
struct U { T a[2]; int u; U() : u(1) { printf("U()\n"); } ~U() { printf("~U()\n"); } };
struct W { T t[3][2]; };
int arr1();
int arr2();
int arr3();
int arr4();
