#include "g.h"
int f1() { int i = 0; G g(&i); return i; }
int f2(bool c) { int i = 5; G g(&i); if (c) return i; i = 9; return i; }
int f3() { int i = 0; for (;;) { G g(&i); if (i == 0) return i; } }
