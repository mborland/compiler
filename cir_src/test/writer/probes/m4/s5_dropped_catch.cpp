// S5 probe: typeinfo introduced by a function that is dropped as unsupported
// (a catch of a class and of an int, inline assembly makes the
// function unsupported): the typeinfo objects stay declared and the program
// still builds; the surviving function runs.
#include <cstdio>
struct Cmp { int x; virtual ~Cmp() {} };
void thrower(int k) { if (k == 1) throw 1; if (k == 2) throw Cmp(); }
static void dropped(int k) {
  try { thrower(k); } catch (Cmp &) { std::puts("Cmp"); } catch (int) { std::puts("int"); }
  asm volatile("nop");
}
int main() {
  try { thrower(1); } catch (int i) { std::printf("int %d\n", i); }
  try { thrower(2); } catch (Cmp &) { std::puts("Cmp"); }
  return 0;
}
