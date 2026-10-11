// Value-initialization zeroes the padding too (CWG 2796-style expectations).
extern "C" int printf(const char *, ...);
extern "C" void *memcpy(void *, const void *, unsigned long);
extern "C" void *memset(void *, int, unsigned long);
inline void *operator new(unsigned long, void *p) noexcept { return p; }
struct A { char c; int i; };
struct B { A a; B (char x, int y) : a () { a.c = x; a.i = y; } };
int main() {
  unsigned char buf[sizeof (B)];
  A a1 = A ();
  memcpy (buf, &a1, sizeof (A));
  printf("a1 %d\n", buf[1]);
  alignas(4) unsigned char m1[sizeof (A)];
  memset (m1, -1, sizeof (m1));
  A *a2 = new (m1) A ();
  memcpy (buf, a2, sizeof (*a2));
  printf("a2 %d\n", buf[1]);
  B b1 (42, -42);
  memcpy (buf, &b1, sizeof (b1));
  printf("b1 %d\n", buf[1]);
  alignas(4) unsigned char m2[sizeof (B)];
  memset (m2, -1, sizeof (m2));
  B *b2 = new (m2) B (1, 2);
  memcpy (buf, b2, sizeof (*b2));
  printf("b2 %d\n", buf[1]);
  return 0;
}
