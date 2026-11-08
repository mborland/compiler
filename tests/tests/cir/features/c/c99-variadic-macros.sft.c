//type:rp
//options:--c99:--c99 -O2
//require:BACK_END_TYPE 3
#include <stdarg.h>

/* Variadic macros (__VA_ARGS__, stringizing and token pasting) and the _Pragma
   operator are handled by the front end before the writer sees the code. */
#define SUM3(...) sum3(__VA_ARGS__)
#define COUNT_(a, b, c, n, ...) n
#define COUNT(...) COUNT_(__VA_ARGS__, 3, 2, 1, 0)
#define STR(...) #__VA_ARGS__
#define CALL(f, ...) f(__VA_ARGS__)
#define PRAGMA(x) _Pragma(#x)

static int sum3(int a, int b, int c)
{
  return a + b + c;
}

static int sum_all(int n, ...)
{
  va_list ap;
  int total = 0;

  va_start(ap, n);
  while (n-- > 0)
    total += va_arg(ap, int);
  va_end(ap);
  return total;
}

PRAGMA(pack(push, 1))
struct packed { char c; int i; };
PRAGMA(pack(pop))
struct natural { char c; int i; };

int main(void)
{
  int failures = 0;
  const char *text = STR(a, b + c);

  if (SUM3(1, 2, 3) != 6)
    failures |= 1;
  if (COUNT(x, y, z) != 3 || COUNT(x, y) != 2 || COUNT(x) != 1)
    failures |= 2;
  if (CALL(sum_all, 3, 10, 20, 30) != 60)
    failures |= 4;
  if (text[0] != 'a' || text[1] != ',' || text[2] != ' ' || text[3] != 'b'
      || text[4] != ' ' || text[5] != '+' || text[8] != '\0')
    failures |= 8;
  if (sizeof(struct packed) != 5 || sizeof(struct natural) != 8)
    failures |= 16;
  return failures;
}
