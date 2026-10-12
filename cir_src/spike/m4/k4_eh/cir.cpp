#include "p.h"
#include <new>
#include <exception>
int nested_try(int k) {
  int r = 0;
  try {
    T a(1);
    try {
      T b(2);
      if (k == 1) throw 11;
      if (k == 2) throw Ex(12);
      if (k == 3) throw 3.5;
    } catch (int i) { r = i; T c(3); if (k == 1) throw ExD(13); }
    catch (const Ex &e) { r = e.code; throw; }
    T d(4);
  } catch (const ExD &e) { r += e.code; }
  catch (Ex &e) { r += e.code * 2; }
  catch (...) { r = -1; }
  return r;
}
int catch_all_rethrow(int k) {
  try {
    try { T a(1); if (k) throw k; }
    catch (...) { T b(2); throw; }
  } catch (int i) { return i * 2; }
  return 0;
}
int jumps(int k) {
  int s = 0;
  for (int i = 0; i < 4; ++i) {
    try {
      T a(i);
      if (i == 1 && k == 0) continue;
      if (i == 2 && k == 1) break;
      if (i == 3 && k == 2) return s + 100;
      if (i == 0) throw i + 7;
      s += i;
    } catch (int v) {
      T h(50 + v);
      if (k == 3) continue;
      if (k == 4) break;
      if (k == 5) return s + 200;
      s += v;
    }
  }
  try { T z(9); if (k == 6) goto out; s += 1; } catch (...) { }
out:
  return s;
}
int ret_in_try(int k) {
  T outer(1);
  try { T a(2); if (k) return k * 2; T b(3); } catch (...) { return -1; }
  return 7;
}
int cleanups_in_try(int k) {
  try {
    T a(1); Thrower t1(0); T b(2);
    if (k == 1) { Thrower t2(1); }
    T c(3);
    Thrower t3(k == 2);
    T d(4);
    return 0;
  } catch (int i) { return i; }
}
struct Res { int v; Res(int x, int fail) : v(x) { printf("Res(%d)\n", x); if (fail) throw fail; } ~Res() { printf("~Res\n"); } };
static int freed = 0;
struct NewT { int pad; NewT(int fail) { printf("NewT(%d)\n", fail); if (fail) throw fail; }
  static void *operator new(unsigned long n) { printf("op new\n"); return ::operator new(n); }
  static void operator delete(void *p) { printf("op delete\n"); ++freed; ::operator delete(p); } };
int new_throws(int k) {
  try {
    NewT *p = new NewT(k);          // free-on-throw when the ctor throws
    delete p;
    T *q = new T(1);
    Res *r = new Res(2, k == 2);    // ordinary operator new
    delete r; delete q;
    T *arr = new T[2]{T(5), T(6)};  // array new with initializers
    delete[] arr;
  } catch (int i) { return i; }
  return 0;
}
int mk(int k) { if (k) throw k; return 1; }
int throw_temps(int k) {
  try {
    T a(1);
    if (k == 1) throw T(2);                      // throw with a temporary
    if (k == 2) throw Ex(mk(3));                 // the thrown object's initializer throws
    if (k == 3) throw Ex(mk(0) + T(4).id);
    if (k == 4) { int x = T(5).id + mk(k); (void)x; }
  } catch (const T &t) { return t.id; }
  catch (const Ex &e) { return e.code; }
  catch (int i) { return i; }
  return 0;
}
int ternary_throw(int k) {
  try {
    int x = k ? 1 : throw Ex(77);
    T t(x);
    return x + (k > 1 ? mk(0) : (throw 5));
  } catch (const Ex &e) { return e.code; }
  catch (int i) { return i + 1000; }
}
struct Mem { T a; Thrower b; T c; Mem(int k) try : a(1), b(k), c(3) { printf("Mem body\n"); } catch (...) { printf("Mem handler\n"); } ~Mem() { printf("~Mem\n"); } };
struct Mem2 { T a; Thrower b; Mem2(int k) try : a(1), b(k) { printf("Mem2 body\n"); if (k == 7) throw 70; } catch (int i) { printf("Mem2 handler %d\n", i); throw i + 1; } };
int ctor_try(int k) {
  try { Mem m(k); printf("constructed\n"); } catch (int i) { return i; }
  try { Mem2 m(k == 0 ? 7 : k); } catch (int i) { return 100 + i; }
  return 0;
}
static void on_terminate() { printf("terminate called\n"); throw 1; }
struct Dtor { ~Dtor() noexcept(true) { printf("~Dtor\n"); } };
void nt(int k) noexcept { if (k) throw k; printf("nt ok\n"); }
int nothrow_term(int k) {
  std::set_terminate([] { printf("terminate\n"); (fflush_all(), _exit(3)); });
  if (k == 0) { nt(0); return 0; }
  try { nt(k); } catch (...) { return -1; }
  return 1;
}
struct Base { virtual ~Base() {} int b = 1; };
struct Dv : Base { int d = 2; };
struct Pol { int x = 9; Pol() {} Pol(const Pol &o) : x(o.x + 1) { printf("Pol copy\n"); } };
int catch_kinds(int k) {
  int r = 0;
  try {
    switch (k) {
    case 0: throw 5;
    case 1: throw Triv{1, 2};
    case 2: throw T(7);
    case 3: { Dv *d = new Dv; throw d; }
    case 4: throw "str";
    case 5: throw Dv();
    case 6: throw Pol();
    case 7: throw 2.5f;
    }
  }
  catch (int i) { r = i; }                          // scalar by value
  catch (Triv t) { r = t.a + t.b; }                 // trivially copyable by value
  catch (T t) { r = t.id; }                         // by value, non-trivial copy
  catch (Base *b) { r = b->b + 20; delete b; }      // derived pointer converted to base pointer
  catch (const char *s) { r = s[0]; }
  catch (Base b) { r = b.b + 40; }                  // by value, slicing
  catch (const Pol &p) { r = p.x; }                 // by reference
  catch (float) { r = 99; }
  return r;
}
struct NR { int v; NR(int x) : v(x) { printf("NR(%d)\n", x); } NR(const NR &o) : v(o.v) { printf("NR copy\n"); } ~NR() { printf("~NR(%d)\n", v); } };
NR nrvo_fn(int k) { NR r(k); T t(1); if (k == 2) throw 2; if (k == 3) { NR other(9); return other; } return r; }
int nrvo(int k) { try { NR x = nrvo_fn(k); return x.v; } catch (int i) { return i; } }
int throw_ctor_fail(int k) {
  try { throw Ex(mk(k)); } catch (const Ex &e) { return e.code; } catch (int i) { return i; }
}
int loops_with_try(int k) {
  int s = 0;
  for (T t(1); s < 3; ++s) { try { T a(2); if (s == k) throw s; } catch (int) { s += 10; } }
  while (true) { try { T w(3); if (k) break; return -5; } catch (...) {} }
  do { try { s += 1; if (s < 100) continue; } catch (...) {} } while (false);
  return s;
}
int run_all() {
  int s = 0;
  for (int k = 0; k < 4; ++k) s += nested_try(k);
  for (int k = 0; k < 2; ++k) s += catch_all_rethrow(k);
  for (int k = 0; k < 7; ++k) s += jumps(k);
  for (int k = 0; k < 2; ++k) s += ret_in_try(k);
  for (int k = 0; k < 3; ++k) s += cleanups_in_try(k);
  for (int k = 0; k < 3; ++k) s += new_throws(k);
  for (int k = 0; k < 5; ++k) s += throw_temps(k);
  for (int k = 0; k < 3; ++k) s += ternary_throw(k);
  for (int k = 0; k < 4; ++k) s += ctor_try(k);
  for (int k = 0; k < 8; ++k) s += catch_kinds(k);
  for (int k = 0; k < 4; ++k) s += nrvo(k);
  for (int k = 0; k < 2; ++k) s += throw_ctor_fail(k);
  for (int k = 0; k < 3; ++k) s += loops_with_try(k);
  s += nothrow_term(0);
  return s;
}
