extern "C" int printf(const char *, ...);
// Nesting cases for deactivation: temporaries with destructors live to the end
// of the full-expression while the storage-freeing / exception-freeing eh scope
// of a new or throw expression is already closed.
struct A { int id; A(int i) : id(i) { printf("A(%d)\n", id); } A(const A &o) : id(o.id + 10) { printf("A copy\n"); } ~A() { printf("~A(%d)\n", id); } };
struct B { int id; B(int i) : id(i) { printf("B(%d)\n", id); } ~B() { printf("~B(%d)\n", id); } };
struct T {
  int v;
  T(const A &a, const B &b, int fail) : v(a.id + b.id) { printf("T(%d)\n", fail); if (fail) throw fail; }
  T(const A &a, int fail) : v(a.id) { printf("T1(%d)\n", fail); if (fail) throw fail; }
  ~T() { printf("~T\n"); }
  static void *operator new(unsigned long n);
  static void operator delete(void *p);
  static void *operator new[](unsigned long n);
  static void operator delete[](void *p);
};
struct U { int v; U() : v(0) { printf("U()\n"); } U(const A &a, int fail) : v(a.id) { printf("U(%d)\n", fail); if (fail) throw fail; } ~U() { printf("~U\n"); } };
int new_two_temps(int fail);
int throw_with_temp(int fail);
int new_array_init(int fail);
int cond_new(int c, int fail);
int cond_new2(int c, int fail);
int new_arg_throws(int fail);
int run_all();
