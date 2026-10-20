//type:fp
//options:--c11 --gcc
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// Supp keys 4 (pure) and 5 (const) on DECL_FUNC: the GNU attributes (sticky: a
// later declaration may add them; both given, const wins in the consumer and
// both keys are sent), and the C library builtins that Clang's table marks
// pure (U) or const (c): isalnum, tolower, abs, fabs, wcslen. Not sent: sin
// (const only without math-errno), strlen (not in the table), a static
// function that has a library name, and a function with an asm label.
int __attribute__((pure)) pure_fn(int *p);
int __attribute__((const)) const_fn(int x);
int __attribute__((pure, const)) both_fn(int x);
int later_fn(int x);
extern int isalnum(int);
extern int tolower(int);
extern int abs(int);
extern double fabs(double);
extern double sin(double);
extern unsigned long strlen(const char *);
extern unsigned long wcslen(const int *);
static int toupper(int c) { return c; }
extern int isdigit(int) __asm__("my_isdigit");
int use(int i, int *p, const char *s, const int *w) {
  return pure_fn(p) + const_fn(i) + both_fn(i) + later_fn(i) + isalnum(i) +
         tolower(i) + abs(i) + (int)fabs((double)i) + (int)sin((double)i) +
         (int)strlen(s) + (int)wcslen(w) + toupper(i) + isdigit(i) +
         __builtin_isalpha(i);
}
int later_fn(int x) __attribute__((const));
int later_fn(int x) { return x; }
