#include "p.h"
extern "C" int printf(const char *, ...);
struct T { int id; T(int i) : id(i) {} ~T() { printf("~T(%d)\n", id); } };
static void thrower(int v) { T t(v); if (v >= 0) throw v; }
static void ok(int) {}
static int cmp_throw(const void *a, const void *b) { static int n = 0; if (++n > 2) throw n; return *(const int *)a - *(const int *)b; }
int main() {
  try { c_call(thrower, 4); printf("not reached\n"); } catch (int i) { printf("caught %d\n", i); }
  c_call(ok, 1); printf("ok path\n");
  try { c_deep(thrower, 50); } catch (int i) { printf("caught deep %d\n", i); }
  int a[5] = {5, 4, 3, 2, 1};
  try { c_array_cb(cmp_throw, a, 5); printf("sorted\n"); } catch (int i) { printf("caught qsort %d\n", i); }
  return 0;
}
