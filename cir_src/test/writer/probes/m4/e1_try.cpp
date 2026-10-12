// M4a probe: try/catch IL and catch-parameter kinds (a_handler.dynamic_init).
#include <cstdio>
struct T { int v; T(int x = 0) : v(x) {} T(const T &o) : v(o.v) { std::puts("T copy"); } ~T() { std::puts("~T"); } };
struct Tr { int v; };                                           // trivially copyable
struct Base { virtual ~Base() {} };
struct Der : Base { };
void thr(int k) {
  switch (k) {
  case 0: throw 1;
  case 1: throw T(2);
  case 2: throw Tr{3};
  case 3: { static int s = 4; throw &s; }
  case 4: throw (const char *)"str";
  case 5: throw Der();
  case 6: throw 2.5;
  }
}
int main() {
  for (int k = 0; k < 7; ++k) {
    try { thr(k); }
    catch (int i) { std::printf("int %d\n", i); }                // scalar
    catch (T t) { std::printf("T by value %d\n", t.v); }         // non_trivial_copy
    catch (Tr t) { std::printf("Tr by value %d\n", t.v); }       // trivial_copy
    catch (int *p) { std::printf("int* %d\n", *p); }             // pointer
    catch (const char *const &s) { std::printf("cc& %s\n", s); } // reference_to_pointer
    catch (Base &b) { std::printf("Base&\n"); }                  // reference
    catch (...) { std::puts("..."); }                            // catch all
  }
  try { try { throw 7; } catch (...) { throw; } } catch (int i) { std::printf("rethrown %d\n", i); }
  try { throw 1; } catch (const int &) { std::puts("const int&"); }
  try { throw 1; } catch (int) { std::puts("unnamed"); }
  return 0;
}
