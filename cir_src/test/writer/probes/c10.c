struct S { int x; int arr[3]; };
struct S mk(void);
int use(void) {
  int a = mk().x;
  int b = mk().arr[1];
  struct S s = mk();
  int c = (s).x;
  return a + b + c;
}
