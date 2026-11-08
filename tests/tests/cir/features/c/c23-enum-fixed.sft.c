//type:rp
//options:--c23:--c23 -O2
//require:BACK_END_TYPE 3
/* Enumerations with a fixed underlying type (C23): the size, signedness and
   the range of the enumerators follow the underlying type; an enumeration
   declared without a list is a complete type. */
enum small : unsigned char { s0, s1, s255 = 255 };
enum wide : long long { w_big = 1LL << 40, w_neg = -3 };
enum shorts : short { sh_min = -32768, sh_max = 32767 };
enum opaque : int;
enum opaque : int { o_a = 1, o_b };

struct packed { enum small kind; enum shorts value; };

static enum small next(enum small v)
{
  return (enum small)(v + 1);           /* wraps like unsigned char */
}

int main(void)
{
  enum wide w = w_big;
  enum small s = s255;
  struct packed p = { s1, sh_min };
  int failures = 0;

  if (sizeof(enum small) != 1 || sizeof(enum wide) != 8
      || sizeof(enum shorts) != 2 || sizeof(enum opaque) != 4)
    failures |= 1;
  if (w != (1LL << 40) || w_neg >= 0)
    failures |= 2;
  if (next(s) != s0 || (enum small)(s - 1) != 254)
    failures |= 4;
  if (sizeof(struct packed) != 4 || p.kind != s1 || p.value != -32768)
    failures |= 8;
  if (o_b != 2)
    failures |= 16;
  return failures;
}
