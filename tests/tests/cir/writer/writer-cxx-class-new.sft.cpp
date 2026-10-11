//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
extern "C" int printf(const char *, ...);
extern "C" void *malloc(unsigned long);
extern "C" void free(void *);
static int log_n;
struct C {
  int v;
  C() : v(3) {}
  explicit C(int x) : v(x) {}
  ~C() { printf("~C %d\n", v); }
  static void *operator new(unsigned long sz) { printf("C::new %lu\n", sz); return malloc(sz); }
  static void operator delete(void *p) { printf("C::delete\n"); free(p); }
  static void *operator new(unsigned long sz, int tag) { printf("C::new tag %d %lu\n", tag, sz); return malloc(sz); }
  static void operator delete(void *p, int) { printf("C::delete tag\n"); free(p); }
};
struct D {
  int v;
  D() : v(1) {}
  ~D() {}
  static void *operator new[](unsigned long sz) { printf("D::new[] %lu\n", sz); return malloc(sz); }
  static void operator delete[](void *p) { printf("D::delete[]\n"); free(p); }
};
struct E { int v; E() : v(5) { log_n++; } };
inline void *operator new(unsigned long, void *p) noexcept { return p; }
int main() {
  C *a = new C;
  C *b = new C(7);
  C *c = new (9) C(11);
  printf("%d %d %d\n", a->v, b->v, c->v);
  delete a;
  delete b;
  C::operator delete(c, 9);
  D *d = new D[3];
  printf("%d\n", d[2].v);
  delete[] d;
  alignas(E) char buf[sizeof(E)];
  E *e = new (buf) E;
  printf("%d %d\n", e->v, log_n);
  return 0;
}
