//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// An if with an integral constant condition sends only the arm that is taken;
// an arm that contains a label (a goto or a case can reach it) is kept with
// the if.  if constexpr is unchanged.
extern "C" int printf(const char *, ...);
void undefined_fn();
void link_error_fn();
const int N = 4;
enum { K = 2 };
int a1(int x) { if (0) undefined_fn(); return x; }
int a2(int x) { if (sizeof(int) == 4) x += 1; else undefined_fn(); return x; }
int a3(int x) { if (N > 5) { undefined_fn(); return 0; } else if (K == 2) { x += 2; } else { link_error_fn(); } return x; }
int a4(int x) {
  if (0) { dead: x += 100; }
  if (x > 1000) goto dead;
  return x;
}
int a5(int x) {
  switch (x) {
    case 1: if (0) { case 2: x += 10; } x += 1; break;
    default: x += 2;
  }
  return x;
}
int a6(int x) { if (1) { int y = x * 2; x = y; } return x; }
int a7(int x) { if (!1) undefined_fn(); else x++; return x; }
int a8(int x) { if (1) ; return x; }
int a9(int x) { for (int i = 0; i < 2; i++) { if (0) continue; x += i; } return x; }
template <int T> int b1(int x) { if (T > 3) { return x + 1; } else { undefined_fn(); } return x; }
int c1(int x) { if constexpr (K == 2) x += 3; else undefined_fn(); return x; }
int main() {
  printf("%d %d %d\n", a1(1), a2(1), a3(1));
  printf("%d %d %d\n", a4(1), a5(1), a5(2));
  printf("%d %d %d %d\n", a6(3), a7(3), a8(3), a9(0));
  printf("%d\n", b1<5>(1));
  return 0;
}
