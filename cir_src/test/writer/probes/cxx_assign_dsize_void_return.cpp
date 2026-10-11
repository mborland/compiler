extern "C" int printf(const char *, ...);
struct B { int i; char c; B() : i(1), c(2) {} B &operator=(const B &) = default; };
struct D : B { char d; D() : d(9) {} };
void assign(B &dst, const B &src) { dst = src; }
B make() { B b; b.i = 7; b.c = 8; return b; }
void assign_call(B *p) { *p = make(); }
void assign_cond(B *p, B *q, bool c) { *p = c ? *q : *p; }
void v1(int *p) { *p += 1; }
void v2(int *p) { return v1(p); }
int main() {
  D x; B y; y.i = 100; y.c = 50;
  assign(x, y);
  printf("%d %d %d\n", x.i, (int)x.c, (int)x.d);
  D z; assign_call(&z); printf("%d %d %d\n", z.i, (int)z.c, (int)z.d);
  D w; assign_cond(&w, &y, true); printf("%d %d %d\n", w.i, (int)w.c, (int)w.d);
  static_cast<B &>(w) = make(); printf("%d %d %d\n", w.i, (int)w.c, (int)w.d);
  int n = 1; v2(&n); printf("%d\n", n);
  return 0;
}
