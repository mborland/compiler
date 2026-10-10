//type:fp
//options:--c11
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
struct P { int a; int b[3]; };
int gi = 5;
int garr[4] = { 1, 2 };
struct P gp = { 1, { 2, 3 } };
struct P gp2 = { .b = { [1] = 7 }, .a = 4 };
int garr2[] = { [2] = 9, 1 };
char gstr[] = "abc";
const char *gcp = "hello";
const char *gnames[] = { "a", "b", "a" };
int *gptr = &gi;
double gd = 1.5;
float gf = 2.25f;
union { int i; float f; } gun = { 3 };
struct P *gpp = &gp;
int local_init(int x) {
  struct P lp = { x, { 1, 2, 3 } };
  int la[3] = { x, 2 };
  static int ls = 4;
  static const char *lnames[] = { "x", "y" };
  return lp.a + la[0] + ls + (int)lnames[0][0];
}
