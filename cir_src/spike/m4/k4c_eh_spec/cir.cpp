#include "p.h"
void only_int(int k) throw(int) { T t(1); if (k == 1) throw 11; if (k == 2) throw 2.5; printf("only_int ok\n"); }
void int_or_bad(int k) throw(int, std::bad_exception) { T t(2); if (k == 1) throw 12; if (k == 2) throw 'c'; }
static void unexpected_handler() { printf("unexpected\n"); throw 77; }
void caller(int k) {
  try { only_int(k); } catch (int i) { printf("caught int %d\n", i); } catch (double) { printf("caught double?\n"); }
}
int run_all() {
  std::set_unexpected(unexpected_handler);
  for (int k = 0; k < 3; ++k) caller(k);
  for (int k = 0; k < 3; ++k) {
    try { int_or_bad(k); } catch (int i) { printf("caught int %d\n", i); } catch (std::bad_exception &) { printf("bad_exception\n"); } catch (...) { printf("other\n"); }
  }
  std::set_unexpected([] { printf("unexpected2\n"); throw; });   // rethrow: becomes bad_exception
  try { int_or_bad(2); } catch (std::bad_exception &) { printf("bad_exception2\n"); } catch (...) { printf("other2\n"); }
  return 0;
}
