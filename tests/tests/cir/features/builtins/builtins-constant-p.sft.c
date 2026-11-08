//type:rp
//options:--gcc:--gcc -O2
//require:BACK_END_TYPE 3
/* __builtin_constant_p outside constexpr functions: the front end answers it,
   1 for a constant expression and 0 for anything else, where GCC at -O2 also
   answers 1 for a value that inlining and propagation make constant (the
   test accepts both answers there: it asks only for the facts that both
   compilers give).  Inside a C++ constexpr function the call reaches the
   writer and is diagnosed (builtins-constant-p-negative.sft.cpp). */
extern int runtime_value(void);

static inline int either(int x)
{
  return __builtin_constant_p(x);       /* 0 or 1: both are right */
}

int runtime_value(void)
{
  return 5;
}

int main(void)
{
  int failures = 0;
  int n = runtime_value();

  if (!__builtin_constant_p(3) || !__builtin_constant_p(sizeof(int)))
    failures |= 1;
  if (__builtin_constant_p(runtime_value()))
    failures |= 2;
  if (either(7) != 0 && either(7) != 1)
    failures |= 4;
  /* A use that depends on the answer being only a hint. */
  if ((__builtin_constant_p(n) ? n : n + 0) != 5)
    failures |= 8;
  return failures;
}
