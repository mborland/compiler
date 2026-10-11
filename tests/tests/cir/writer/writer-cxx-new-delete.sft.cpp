//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
extern "C" void *malloc(unsigned long);
struct T { int id; T(); T(int); ~T(); };
struct P { int v; };
void *operator new(unsigned long, void *p) noexcept;
int f(int n) {
  int *a = new int;
  int *b = new int(5);
  int *c = new int[n];
  T *t1 = new T;
  T *t2 = new T(3);
  T *t3 = new T[n];
  T *t4 = new T[2]{ T(1), T(2) };
  P *p1 = new P();
  char buf[16];
  T *pl = new (buf) T(1);
  delete a; delete b; delete[] c; delete t1; delete t2; delete[] t3; delete[] t4; delete p1;
  pl->~T();
  return 0;
}
struct C { int v; C(); ~C(); static void *operator new(unsigned long sz); static void operator delete(void *p, unsigned long sz); };
void class_new() { C *c = new C; delete c; }
struct D { int v; static void operator delete(void *p); };
void class_delete(D *d) { delete d; }
struct A32 { alignas(32) int x; A32(); ~A32(); };
void aligned() { A32 *p = new A32[2]; delete[] p; }
