#include "s3_mp_adj_p.h"
int main() {
  int s = run_all();
  printf("run_all %d\n", s);
  D d; d.b1 = 7; d.b2 = 8; d.d = 9;
  // g++-created member pointers consumed by CIR code, and vice versa
  DMethod gm[6] = { &D::vf, &D::g, &D::dn, &B1::nf, &B2::ng, nullptr };
  for (int i = 0; i < 5; ++i) printf("%d ", apply(&d, gm[i], 2));
  printf("\n");
  for (int i = 0; i < 6; ++i) printf("%d ", g_methods[i] == gm[i]);
  printf("\n");
  for (int i = 0; i < 4; ++i) printf("%d ", g_datas[i] == (i == 0 ? &D::b1 : i == 1 ? &D::b2 : i == 2 ? &D::d : (DData)nullptr));
  printf("\n");
  for (int w = 0; w < 6; ++w) { DMethod m = pick(w); printf("%d ", m == gm[w]); }
  printf("\n");
  for (int i = 0; i < 6; ++i) { long *a = (long *)&g_methods[i], *b = (long *)&gm[i]; printf("m%d: %lx %ld | %lx %ld\n", i, 0L + (a[0] != 0), a[1], 0L + (b[0] != 0), b[1]); }
  B2 *pb2 = &d; printf("%d %d\n", (pb2->*g_bmethods[0])(1), (pb2->*g_bmethods[1])(1));
  return 0;
}
