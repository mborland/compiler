// Host of cxx-s4-anonymous-member-pointers.test: S has an anonymous union and
// an anonymous struct; the CIR unit defines pointers to their members.
struct S {
  int pad;
  union { int a; float b; };
  struct { int c; int d; };
};
extern int S::*g_pa;
extern float S::*g_pb;
extern int S::*g_pc;
extern int S::*g_pd;

int main() {
  S s;
  s.pad = 1; s.a = 2; s.c = 3; s.d = 4;
  if (s.*g_pa != 2) return 1;
  s.b = 2.5f;
  if (s.*g_pb != 2.5f) return 2;
  if (s.*g_pc != 3) return 3;
  if (s.*g_pd != 4) return 4;
  s.*g_pd = 9;
  if (s.d != 9) return 5;
  return g_pa == &S::a && g_pb == &S::b && g_pd == &S::d ? 0 : 6;
}
