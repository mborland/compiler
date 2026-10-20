//type:fp
//options:--c++17 --g++ --gnu_version=140000
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// NOTHROW (0x80) on the library functions that Clang's builtin table marks
// nothrow (the letter n), under the rule of pure and const: extern "C" (also
// in a namespace), the library name and signature. Not for the cancellation
// points fprintf, vprintf and vfprintf, nor for a function that takes a
// callback or can run user code (qsort and atexit are not nothrow in the
// table either), nor for a C++ function or a member with the name.
extern "C" {
unsigned long strlen(const char *);
void *memcpy(void *, const void *, unsigned long);
double sin(double);
int sprintf(char *, const char *, ...);
int fprintf(void *, const char *, ...);
void qsort(void *, unsigned long, unsigned long,
           int (*)(const void *, const void *));
int atexit(void (*)(void));
void abort(void);
int isalnum(int);
}
namespace ns { unsigned long strlen(const char *); extern "C" int abs(int); }
struct S { unsigned long strlen(const char *); };
struct Guard { ~Guard(); };
void use(char *b, S &s, void *f) {
  Guard g;
  strlen(b);
  memcpy(b, b, 1);
  sin(1.0);
  sprintf(b, "x");
  fprintf(f, "x");
  qsort(b, 1, 1, 0);
  atexit(0);
  isalnum(1);
  ns::strlen(b);
  ns::abs(1);
  s.strlen(b);
  abort();
}
