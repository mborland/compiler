//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// A new expression frees its storage if the initialization throws (spec
// 12.6, encoding 10): the size is kept in a local before the allocation call,
// the result in a pointer local, CLEANUP_BEGIN(eh, DEACTIVATABLE) calls the
// matching operator delete (sized, aligned, the class's own, placement with
// the placement operands kept in locals), and CLEANUP_END follows the
// initialization. Nothing for an initialization that cannot throw, or for the
// global placement new.
struct A { A(); ~A(); int v; };
struct T { T(const A &); ~T(); int w; };
struct U { U(int); int u; };
struct alignas(64) O { O(); int x; };
struct PL {
  PL(int);
  static void *operator new(unsigned long n, int);
  static void operator delete(void *p, int);
  static void operator delete(void *p);
};
struct CD { CD(); static void operator delete(void *p, unsigned long); };
void *operator new(unsigned long, int);
void operator delete(void *, int);
int g();
T *n1() { return new T(A()); }
U *n2(bool c) { return c ? new U(1) : 0; }
U *n3() { return new (3) U(2); }
int *n4() { return new int(5); }
int *n5() { return new int(g()); }
O *n6() { return new O; }
PL *n7() { return new (3) PL(1); }
CD *n8() { return new CD; }
A *n9(int n) { return new A[n]; }
int *sp = new int(g());
