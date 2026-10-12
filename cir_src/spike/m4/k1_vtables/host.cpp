#include "p.h"
// g++-compiled derived classes of CIR-compiled bases, and objects created here.
struct HB : B { int hb; HB() : hb(11) { printf("HB() %d\n", whoami()); } ~HB() override { printf("~HB\n"); } int whoami() override { return 11; } B *self() override { return this; } };
struct HD : virtual V { int hd; HD() : hd(12) { printf("HD() %d\n", vf()); } ~HD() override { printf("~HD\n"); } int vf() override { return 120 + v; } int whoami() override { return 12; } };
struct HE : HD, L { HE() { printf("HE() %d\n", vf()); } ~HE() override { printf("~HE\n"); } int vf() override { return 130 + v + l + hd; } int whoami() override { return 13; } };
int main() {
  int s = run_all();
  printf("sum %d\n", s);
  { HB hb; A *pa = &hb; printf("%d %d %d\n", pa->f(3), pa->whoami(), pa->self()->whoami()); }
  { HE he; V *pv = &he; printf("%d %d\n", pv->vf(), pv->whoami()); L *pl = &he; printf("%d\n", pl->vf()); }
  { D *d = new D; V *pv = d; R *pr = d; printf("%d %d\n", pv->vf(), pr->whoami()); delete pr; }
  { E e; N *pn = &e; A *pa = &e; printf("%d %d\n", pn->f(7), pa->f(7)); }
  return s != 0 ? 0 : 1;
}
