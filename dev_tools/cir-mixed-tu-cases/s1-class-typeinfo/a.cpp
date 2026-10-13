#include "common.h"

int A::f() { return 1; }
A::~A() {}
int C::g() { return 2; }
C::~C() {}
A *make_a() { return new A; }
