// M4a probe: range-for with a try block in the body and with a throwing begin()
// / operator*; the range-for desugaring in the IL and the cleanup of __range.
#include <cstdio>
bool g_throw = false;
struct It { int *p; int &operator*() const { if (*p == 5 && g_throw) throw 3; return *p; } It &operator++() { ++p; return *this; } bool operator!=(const It &o) const { return p != o.p; } };
struct Rg { int a[5] = {1, 2, 3, 4, 5}; It begin() { return It{a}; } It end() { return It{a + 5}; } ~Rg() { std::puts("~Rg"); } };
Rg mk() { return Rg(); }
int main() {
  for (int i : mk()) {
    try { std::printf("%d\n", i); if (i == 2) throw 'x'; } catch (char c) { std::printf("caught %c\n", c); continue; }
    if (i == 4) break;
  }
  g_throw = true;
  try { for (int i : mk()) std::printf("%d\n", i); } catch (int i) { std::printf("throw %d\n", i); }
  return 0;
}
