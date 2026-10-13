//options: --c++17
// A class value returned through a comma or a conditional: the arms build
// the return slot.
extern "C" int printf(const char *, ...);
struct X {
  int i;
  X(int v) : i(v) { printf("X(%d)\n", v); }
  X(const X &o) : i(o.i + 100) { printf("copy\n"); }
  ~X() { printf("~X(%d)\n", i); }
};
X g() { return X(1), X(2); }
X h(bool c) { return c ? X(3) : X(4); }
int main() {
  { X a = g(); printf("a %d\n", a.i); }
  { X b = h(true); printf("b %d\n", b.i); }
  { X c = h(false); printf("c %d\n", c.i); }
  return 0;
}
