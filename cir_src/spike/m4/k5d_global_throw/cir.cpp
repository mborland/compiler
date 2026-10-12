#include "p.h"
#include <exception>
int mk(int k) { printf("mk(%d)\n", k); if (k == 2) throw k; return k; }
// a handler installed by an earlier initializer, so that the throwing one terminates through it
struct Setup { Setup() { std::set_terminate([] { printf("terminate handler\n"); fflush(nullptr); __builtin_trap(); }); } };
__attribute__((init_priority(101))) Setup setup;
G g1(mk(1));
G g2(mk(2));     // throws during static initialization: std::terminate
G g3(mk(3));
