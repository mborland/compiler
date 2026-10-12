#include "p.h"
void c_call(cb_t cb, int v) { int local = v * 2; cb(local); local += 1; }
int c_deep(cb_t cb, int n) { if (n == 0) { cb(n); return 0; } int r = c_deep(cb, n - 1); return r + 1; }
extern void qsort(void *, unsigned long, unsigned long, int (*)(const void *, const void *));
int c_array_cb(int (*cmp)(const void *, const void *), int *a, int n) { qsort(a, n, sizeof(int), cmp); return a[0]; }
