#include "common.h"

int V::h(int x) { return x; }
int V::k() { return 1; }
D::D() {}
int D::own() { return 60 + d; }
int D::k() { return 70 + d + v; }
D *make_d() { return new D; }
int call_h(V *p) { return p->h(2) * 1000 + p->k(); }
