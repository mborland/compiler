struct R0 { virtual ~R0() {} };
struct R1 : R0 {};
struct A1 { virtual int f() { return 1; } int a1; };
struct A2 { virtual int g(int) { return 2; } virtual R0 *h() { return 0; } int a2; };
struct C : A1, A2 {
  int f() override;
  int g(int) override;
  R1 *h() override;
};
int C::f() { return 7; }
int main() {
  C c;
  A1 *p1 = &c; A2 *p2 = &c;
  if (p1->f() != 7) return 1;
  if (p2->g(5) != 5) return 2;
  if (p2->h() != 0) return 3;
  A2 a2;
  if (a2.g(1) != 2) return 4;
  return 0;
}
