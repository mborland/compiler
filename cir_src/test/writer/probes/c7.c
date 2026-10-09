struct In { int a, b[3]; };
struct Out { struct In i; int c; char s[4]; unsigned bf:3; int z; };
struct Out o1 = { .i.b[1] = 5, .c = 2, .s = "xy", .bf = 3 };
struct Out o2 = { { 1, { 2, 3 } }, 4, "ab", 1, 9 };
int ra[8] = { [2] = 1, [5 ... 6] = 7 };
int *cl = (int[]){ 1, 2 };
static int tent_s;
extern int ex_init = 4;
struct Out oarr[2] = { { .c = 1 }, { .c = 2 } };
void f(void) {
  static struct In si = { 1, { 2 } };
  static int sarr[3] = { 4 };
  static const char *names[] = { "a", "bb" };
  struct Out lo = { .c = 1 };
  int ar[4] = { 0 };
  int br[4] = {};
}
