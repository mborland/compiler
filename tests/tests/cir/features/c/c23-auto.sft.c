//type:rp
//options:--c23:--c23 -O2
//require:BACK_END_TYPE 3
/* Type inference in object definitions (C23 auto). */
struct point { int x, y; };

static long long wide(void) { return 1LL << 40; }

int main(void)
{
  auto i = 5;
  auto d = 2.5;
  auto c = 'a';
  auto u = 7u;
  auto l = wide();
  auto p = &i;
  auto s = (struct point){ 3, 4 };
  auto str = "text";
  auto f = 1.5f;
  int failures = 0;

  if (sizeof(i) != sizeof(int) || sizeof(d) != sizeof(double))
    failures |= 1;
  if (sizeof(c) != sizeof(int) || sizeof(u) != sizeof(unsigned))
    failures |= 2;
  if (sizeof(l) != sizeof(long long) || l != (1LL << 40))
    failures |= 4;
  *p += 10;
  if (i != 15 || s.x + s.y != 7)
    failures |= 8;
  if (str[0] != 't' || sizeof(f) != sizeof(float) || f != 1.5f)
    failures |= 16;
  if (u - 8 < 0)
    failures |= 32;                     /* unsigned wraps: still >= 0 */
  return failures;
}
