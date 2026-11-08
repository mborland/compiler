//type:rp
//options:--c23:--c23 -O2
//use_system_includes: true
//require:BACK_END_TYPE 3
/* C23 preprocessor: #elifdef, #elifndef, #warning, __has_include and
   __VA_OPT__ (the #warning below is taken on purpose: a warning is
   recorded). */
#define DEFINED 1
#undef MISSING

#ifdef MISSING
#define BRANCH 1
#elifdef DEFINED
#define BRANCH 2
#else
#define BRANCH 3
#endif

#ifdef MISSING
#define NOT_BRANCH 1
#elifndef MISSING
#define NOT_BRANCH 2
#else
#define NOT_BRANCH 3
#endif

#if __has_include(<stddef.h>) && !__has_include("no-such-header-anywhere.h")
#define HAS_INCLUDE 1
#else
#define HAS_INCLUDE 0
#endif

#define PICK(a, ...) (a __VA_OPT__(+ __VA_ARGS__))
#define CALL(f, ...) f(0 __VA_OPT__(,) __VA_ARGS__)

#warning "this #warning is taken: C23 standardizes it"

static int one(int a) { return a + 1; }
static int two(int a, int b) { return a + b + 2; }

int main(void)
{
  int failures = 0;

  if (BRANCH != 2 || NOT_BRANCH != 2)
    failures |= 1;
  if (HAS_INCLUDE != 1)
    failures |= 2;
  if (PICK(1) != 1 || PICK(1, 2) != 3)
    failures |= 4;
  if (CALL(one) != 1 || CALL(two, 5) != 7)
    failures |= 8;
  return failures;
}
