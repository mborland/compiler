extern "C" int printf(const char *, ...);
int r; int c;
int f () { if (++c > 1) ++r; return 42; }
const int *p;
void g () {
  static const int &i = f();
  if (p && p != &i) ++r;
  if (i != 42) ++r;
  printf("r=%d i=%d\n", r, i);
  p = &i;
}
void h () { int arr[] = { 1, 1, 1, 1, 1, 1, 1 }; g (); }
int main () { g (); h (); printf("%d\n", r); return r; }
