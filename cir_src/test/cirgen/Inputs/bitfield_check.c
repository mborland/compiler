/* Companion of bitfield.test: gcc checks what the EDG-CIRGen functions do to
   bit-fields and what they read back from gcc-laid-out records. */
struct B { int a:3; int b:5; unsigned c:20; long long d:40; };
struct __attribute__((packed)) PB { char c; int a:4; int b:20; short s; };
extern void fill(struct B *);
extern int sum(struct B *);
extern int incdec(struct B *);
extern int store_result(struct B *);
extern void fillpb(struct PB *);
extern int sumpb(struct PB *);

#define CHECK(n, c) do { if (!(c)) return n; } while (0)

int main(void)
{
  struct B b = {0};
  struct PB pb = {0};
  int r;

  fill(&b);
  CHECK(1, b.a == 3 && b.b == -15 && b.c == 1000000 && b.d == -5);
  b.a = -4; b.b = 9; b.c = 7; b.d = 100;
  CHECK(2, sum(&b) == -4 + 9 + 7 + 100);
  b.a = 3; b.c = 7; b.d = 100;
  r = incdec(&b);
  CHECK(3, r == 3 && b.a == -4 && b.c == 8 && b.d == 99);
  CHECK(4, store_result(&b) == -3 && b.a == -3);
  fillpb(&pb);
  CHECK(5, pb.c == 9 && pb.a == -3 && pb.b == 54321 && pb.s == 77);
  CHECK(6, sumpb(&pb) == 9 - 3 + 54321 + 77);
  return 0;
}
