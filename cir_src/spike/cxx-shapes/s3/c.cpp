#include "t.h"
int c1(bool b) { return b ? T(1).v : 0; }
bool c2(bool a) { return a && T(2).ok(); }
bool c3(bool a, bool b) { return (a ? T(3).ok() : T(4).ok()) && b; }
int c4(bool a) { int r = a ? T(5).v + T(6).v : 7; return r; }
