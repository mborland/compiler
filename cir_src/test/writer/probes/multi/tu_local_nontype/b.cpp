int g2;
static void fr(int x) { g2 += x * 100; }
template <void (*F)(int)> struct Holder { static int run() { F(1); return 20; } };
int tu2() { return Holder<&fr>::run() + g2; }
