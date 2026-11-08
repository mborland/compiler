//type:rp
//options:--c89 --gcc:--c89 --gcc -O2
//require:BACK_END_TYPE 3
/* K&R (old-style) function definitions: the front end turns them into
   prototyped functions.  Parameters narrower than int and float are passed
   promoted (the arguments of a call are default-promoted, as no prototype is
   in scope for a definition of this form).  GNU mode is on only because the
   test's own // directive lines are not C89 comments.  The exit status is 0
   when every check holds. */
int add(a, b)
  int a;
  int b;
{
  return a + b;
}

double scale(x, f)
  float x;
  double f;
{
  return x * f;
}

int widen(c, s)
  char c;
  short s;
{
  return c + s;
}

static int apply(fn, v)
  int (*fn)();
  int v;
{
  return fn(v, 10);
}

int main()
{
  int failures = 0;

  if (add(2, 3) != 5)
    failures |= 1;
  if (scale(2.0f, 1.5) != 3.0)
    failures |= 2;
  if (widen(-5, 300) != 295)
    failures |= 4;
  if (apply(add, 7) != 17)
    failures |= 8;
  return failures;
}
