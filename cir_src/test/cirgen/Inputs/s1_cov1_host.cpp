struct R1 { int a; };
struct R2 { int b; };
struct D2 : R1, R2 { int c; };
struct B1 { virtual R2 *get(); virtual ~B1(); int pad; };
struct D : B1 { D2 d2; ~D() override; D2 *get() override; D(); };
R2 *B1::get() { return 0; }
B1::~B1() {}
D::D() { d2.a = 1; d2.b = 2; d2.c = 3; }
D::~D() {}
int main() {
  D d;
  B1 *p = &d;
  R2 *r = p->get();
  if (r != static_cast<R2 *>(d.get())) return 1;
  if (r->b != 2 || r == nullptr) return 2;
  if ((char *)r != (char *)&d.d2 + 4) return 3;
  B1 b1;
  if (b1.get() != 0) return 4;
  D2 *x = d.get();
  if (x->c != 3) return 5;
  return 0;
}
