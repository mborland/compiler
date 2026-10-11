#include <cstdio>
#include "g.h"
G::G(int *q) : p(q) { std::printf("G(%p)\n", (void*)q); }
G::~G() { ++*p; std::printf("~G: i=%d\n", *p); }
