//type:fp
//options:--c11 --gcc
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// noinline in C (gcc.c-torture/execute/20070507-1): the key is on the
// definitions of bar, foo and loop_next, and not on main.
void __attribute__((noinline)) bar(char *p) { p[255] = 1; }
int __attribute__((noinline)) foo(char *p) { return p != 0; }
int __attribute__((noinline)) loop_next(int *p) {
  static int state = 0;
  if (state == 1) return 1;
  state = 1;
  return 0;
}
int main(void) {
  char buffer[256];
  int t;
  bar(buffer);
  return foo(buffer) + loop_next(&t);
}
