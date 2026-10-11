//options:--c++17
// A reference captured by copy copies the referent.
extern "C" int printf(const char *, ...);
struct P { int a, b; };
int main() {
  int x = 1;
  int &r = x;
  auto l = [r] { return r; };
  x = 5;
  P p = { 3, 4 };
  P &pr = p;
  auto l2 = [pr] { return pr.a * 10 + pr.b; };
  p.a = 9;
  printf("%d %d %d\n", l(), l2(), r);
  return 0;
}
