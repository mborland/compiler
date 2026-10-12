#include "p.h"
static int fail = 1;
int mk(int k) { if (fail && k) { fail = 0; printf("mk throws\n"); throw 5; } return k + 10; }
int once(int k) { static int v = mk(k); return v; }
int twice(int k) { static T t(mk(k)); return t.id; }
V::V() : v(1) {}
V::~V() {}
int count_init(int k) { printf("count_init %d\n", k); return k * 2; }
D::D(int k) : d(k) { static int s = count_init(k); d += s; }
D::~D() { static int s2 = count_init(100); d += s2; }
E::E(int k) : D(k + 1) { static int s3 = count_init(7); (void)s3; }
E::~E() {}
int ctor_static(int k) { try { E e(k); D d(k); return e.d + d.d; } catch (int i) { return i; } }
inline int in_fn(int k) { static int z = count_init(k); return z; }
int inline_static(int k) { return in_fn(k) + in_fn(k + 1); }
