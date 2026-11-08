//type:rp
//options:--c23:--c23 -O2
//require:BACK_END_TYPE 3
/* typeof and typeof_unqual (C23): typeof keeps the qualifiers of its operand,
   typeof_unqual drops them.  Assigning to an object of the unqualified type
   proves it is not const. */
struct pair { int a; const int b; };

int main(void)
{
  const volatile int cv = 5;
  const int array[3] = { 1, 2, 3 };
  typeof_unqual(cv) plain = cv;         /* int */
  typeof(cv) *pointer = &cv;            /* const volatile int * */
  typeof_unqual(array) copy;            /* int[3] */
  typeof_unqual(const struct pair) both = { 1, 2 };
  typeof(int (*)(int)) fn = 0;
  int failures = 0;

  plain += 1;
  copy[0] = array[0];
  copy[1] = array[1] + 10;
  copy[2] = array[2] * 2;
  both.a = 9;
  if (plain != 6 || *pointer != 5)
    failures |= 1;
  if (sizeof(copy) != sizeof(array) || copy[1] != 12 || copy[2] != 6)
    failures |= 2;
  if (both.a != 9 || both.b != 2 || fn != 0)
    failures |= 4;
  if (_Generic((typeof_unqual(cv))0, int: 0, default: 1))
    failures |= 8;
  return failures;
}
