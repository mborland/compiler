//options: --c++11
// The assignment of an empty class copies nothing: through the reference to
// an empty base (at the offset of the first member) it must not write a byte.
extern "C" int printf(const char *, ...);
struct E { };
struct S : E {
  char x;
  E &tail() { return *this; }
};
struct P : E { long v; };
int main() {
  S s;
  s.x = 'q';
  E e;
  s.tail() = e;
  printf("%c\n", s.x);
  P p;
  p.v = 0x1234567890abcdefL;
  static_cast<E &>(p) = e;
  printf("%lx\n", p.v);
  E &r = s;
  r = e;
  (r = e) = e;
  printf("%c\n", s.x);
  return 0;
}
