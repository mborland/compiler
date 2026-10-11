#include "g.h"
int f4() { int i = 0; { G g(&i); if (i == 0) goto out; i = 100; } i = 200; out: return i; }
int f5() { int i = 0; for (int k = 0; k < 3; ++k) { G g(&i); if (k == 1) continue; if (k == 2) break; i += 10; } return i; }
int f6(int n) { int i = 0; switch (n) { case 1: { G g(&i); return i + 50; } default: break; } return i; }
