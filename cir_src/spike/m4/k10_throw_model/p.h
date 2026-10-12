extern "C" int printf(const char *, ...);
namespace std { int uncaught_exceptions() noexcept; }
// K10: the final THROW model.  throw T(A()) in statement position and in a ?:
// arm: the temporary A is destroyed while the exception propagates (clang;
// g++ destroys it before the throw), a throwing T constructor destroys A, then
// frees the exception object.
struct A {
  int id;
  A(int i) : id(i) { printf("A(%d)\n", id); }
  A(const A &o) : id(o.id + 10) { printf("A copy\n"); }
  ~A() { printf("~A(%d) uncaught=%d\n", id, std::uncaught_exceptions()); }
};
struct T {
  int v;
  T(const A &a, int fail) : v(a.id) { printf("T(%d)\n", fail); if (fail) throw fail; }
  T(const T &o) : v(o.v + 100) { printf("T copy\n"); }
  ~T() { printf("~T uncaught=%d\n", std::uncaught_exceptions()); }
};
int stmt_throw(int fail);
int tern_throw(int c, int fail);
int nested(int fail);
int run_all();
