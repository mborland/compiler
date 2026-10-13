// Host of cxx-incomplete-global.test: the variable the CIR code only takes
// the address of (its record type has no body there).
struct E { int v; };
E e;
extern "C" void *c_addr();
int main() { return c_addr() == &e ? 0 : 1; }
