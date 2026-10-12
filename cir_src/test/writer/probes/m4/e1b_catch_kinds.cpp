// M4a probe: remaining catch parameter forms: reference to pointer to record,
// pointer to record, derived-to-base by value (slicing copy), enum, array
// parameter (decays), function pointer.
#include <cstdio>
struct R { int x; };
struct B { B() {} B(const B &) { std::puts("B copy"); } virtual ~B() {} };
struct D : B { };
enum E { e0, e1 };
void f() {}
void run(int k) {
  try {
    switch (k) {
    case 0: { static R r{1}; throw &r; }
    case 1: { static R r{2}; throw &r; }
    case 2: throw D();
    case 3: throw e1;
    case 4: throw f;
    }
  }
  catch (R *&p) { std::printf("R*& %d\n", p->x); }          // reference_to_record_pointer? (non-const ref)
  catch (B b) { std::puts("B by value"); }
  catch (E e) { std::printf("E %d\n", e); }
  catch (void (*p)()) { std::puts("fn ptr"); }
}
int main() { for (int k = 0; k < 5; ++k) { try { run(k); } catch (...) { std::puts("outer"); } } return 0; }
