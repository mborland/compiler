extern "C" int printf(const char *, ...);
extern "C" void *malloc(unsigned long);
extern "C" void *memset(void *, int, unsigned long);
struct T { T() : q(5) {} int q; ~T() {} };
struct S { S() = default; int a; int b; T t; };
struct U { U() = default; int a; int b; };
struct Base { Base() = default; int x; int y; };
struct D : Base { int d; D() : Base() , d(1) {} };
struct H { S s; U u; int z; H() : s(), u() { z = 9; } };
void dirty() { volatile char buf[512]; for (int i = 0; i < 512; i++) buf[i] = (char)0x5A; }
int vi_stack() { S s{}; S s2 = S(); U u = U(); return s.a + s.b + s2.a + s2.b + u.a + u.b + s.t.q; }
S mk() { return S(); }
int vi_new() { U *p = new U(); int r = p->a + p->b; return r; }
int vi_member() { H h; return h.s.a + h.s.b + h.u.a + h.u.b + h.z; }
int vi_base() { D d; return d.x + d.y + d.d; }
int main() {
  dirty(); printf("%d ", vi_stack());
  dirty(); printf("%d ", mk().a + mk().b);
  dirty(); printf("%d ", vi_member());
  dirty(); printf("%d\n", vi_base());
  void *junk = malloc(sizeof(U)); memset(junk, 0x77, sizeof(U));
  return 0;
}
