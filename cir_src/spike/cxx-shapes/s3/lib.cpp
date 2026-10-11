#include <cstdio>
#include "t.h"
T::T(int x) : v(x) { std::printf("T(%d)\n", x); }
T::~T() { std::printf("~T(%d)\n", v); }
bool T::ok() const { return v != 0; }
