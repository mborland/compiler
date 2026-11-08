//type:fn
//options:--c++17
//require:BACK_END_TYPE 3
// The library error for a definition that was dropped because it is the
// implicit copy constructor of a class with an array member of class objects:
// the elements are copied one by one, and the writer does not express that
// copy (it used to be translated without the source argument, so the program
// misbehaved).
extern "C" int printf(const char *, ...);
struct E {
  int v;
  E() : v(0) {}
  E(const E &o) : v(o.v + 1) {}
};
struct Holder { E items[3]; };
int main() {
  Holder h;
  Holder c(h);
  printf("%d %d %d\n", c.items[0].v, c.items[1].v, c.items[2].v);
  return c.items[2].v == 1 ? 0 : 1;
}
