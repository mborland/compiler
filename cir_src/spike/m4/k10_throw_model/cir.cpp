#include "p.h"
int stmt_throw(int fail) {
  try { throw T(A(3), fail); } catch (const T &t) { return t.v; } catch (int i) { return i + 100; }
}
int tern_throw(int c, int fail) {
  try { int r = c ? throw T(A(4), fail) : 1; return r; } catch (const T &t) { return t.v; } catch (int i) { return i + 100; }
}
int nested(int fail) {
  try { try { throw T(A(5), fail); } catch (const T &) { throw; } } catch (const T &t) { return t.v; } catch (int i) { return i + 100; }
}
int run_all() {
  int s = 0;
  for (int f = 0; f < 2; ++f) { printf("-- stmt_throw %d\n", f); s += stmt_throw(f * 7); }
  for (int c = 0; c < 2; ++c) for (int f = 0; f < 2; ++f) { printf("-- tern_throw %d %d\n", c, f); s += tern_throw(c, f * 5); }
  for (int f = 0; f < 2; ++f) { printf("-- nested %d\n", f); s += nested(f * 3); }
  return s;
}
