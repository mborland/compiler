//type:rp
//options:--gcc:--gcc -O2
//require:BACK_END_TYPE 3
/* __builtin_alloca_with_align(size, bits) with an alignment of at most 128
   bits (16 bytes): the library aligns every alloca to 16, so the request is
   honored.  A larger alignment is diagnosed (writer-alloca-align-*.sft.c). */
typedef __SIZE_TYPE__ size_t;

__attribute__((noinline)) static int fill(size_t n, int salt)
{
  unsigned char *p = __builtin_alloca_with_align(n, 128);
  unsigned char *q = __builtin_alloca_with_align(n + 3, 64);
  unsigned char *r = __builtin_alloca_with_align(1, 8);
  size_t i;
  int sum = 0;

  for (i = 0; i < n; i++)
    p[i] = (unsigned char)(i + salt);
  for (i = 0; i < n; i++)
    sum += p[i];
  q[0] = 5;
  r[0] = 9;
  if (((size_t)p & 15) != 0 || ((size_t)q & 7) != 0)
    return -1;
  return sum + q[0] + r[0];
}

int main(void)
{
  int failures = 0;

  if (fill(10, 1) != 55 + 14)
    failures |= 1;
  if (fill(100, 0) != 4950 + 14)
    failures |= 2;
  return failures;
}
