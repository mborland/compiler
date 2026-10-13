struct A1 { virtual int f() { return 1; } int a1; };
struct A2 { virtual int g(int) { return 2; } virtual int k() { return 3; } int a2; };
struct C : A1, A2 {
  int f() override;
  int g(int) override;
  int k() override;
};
int C::f() { return 7; }
int main() {
  C c; c.a2 = 2;
  A1 *p1 = &c; A2 *p2 = &c;
  if (p1->f() != 7) return 1;
  if (p2->g(5) != 6) return 2;
  if (p2->k() != 42) return 3;
  A2 a2; a2.a2 = 0;
  if (a2.g(1) != 2 || a2.k() != 3) return 4;
  return 0;
}
