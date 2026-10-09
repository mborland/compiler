struct P { int x, y; };
extern int puts(const char *);
int (*fpt)(const char *) = puts;
struct P mk(int a) { struct P r = { a, a+1 }; return r; }
int use(struct P *p);
static inline int sinl(int a) { return a + 1; }
inline int cinl(int a) { return a + 2; }
extern inline int einl(int a) { return a + 3; }
int kr(a, b) int a; char b; { return a + b; }
int unproto();
int call(int i) {
  puts("hi");
  const char *s = "abc" "def";
  char buf[4] = "ab";
  int local[3] = { i, 2 };
  struct P p = { .y = i };
  struct P *pp = &(struct P){ 1, 2 };
  int *ip = (int[]){ 1, 2, 3 };
  struct P q = mk(i);
  fpt("x");
  (*fpt)("y");
  int (*f2)(const char*) = &puts;
  f2 = puts;
  unproto(1, 2.0);
  return use(&p) + sinl(i) + cinl(i) + einl(i) + (int)sizeof(buf) + __func__[0];
}
