// Kernel: virtual calls through base pointers (three derived classes).
#include <cstdio>

struct Shape {
  virtual ~Shape() {}
  virtual long area(long k) const = 0;
};
struct Sq : Shape {
  long s;
  explicit Sq(long x) : s(x) {}
  long area(long k) const override { return s * s + k; }
};
struct Rect : Shape {
  long w, h;
  Rect(long a, long b) : w(a), h(b) {}
  long area(long k) const override { return w * h - k; }
};
struct Tri : Shape {
  long b, h;
  Tri(long x, long y) : b(x), h(y) {}
  long area(long k) const override { return b * h / 2 + (k & 3); }
};

int main() {
  Sq a(3);
  Rect b(4, 5);
  Tri c(6, 7);
  Shape *shapes[3] = {&a, &b, &c};
  long sum = 0;
  for (long i = 0; i < 3000000; ++i) sum += shapes[i % 3]->area(i);
  std::printf("%ld\n", sum);
  return 0;
}
