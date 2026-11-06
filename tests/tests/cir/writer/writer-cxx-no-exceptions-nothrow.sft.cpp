//type:fp
//options:--c++17 --no_exceptions --g++ --gnu_version=150200
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// DECL_FUNC flag NOTHROW (128) in a C++ stream without exceptions (M5.1 W1):
// as in a C stream, a definition, a function with the nothrow attribute and
// the library functions that clang's table says do not throw and a
// noexcept (or throw()) declaration have it; a declaration of a function that
// the user writes without one does not, nor does a destructor declaration (its
// implicit specification is not computed here).
typedef unsigned long size_t;
extern "C" size_t strlen(const char *);
int user_decl(int);
int user_nothrow(int) __attribute__((nothrow));
int user_noexcept(int) noexcept;
int user_throw0(int) throw();
struct S { int m(int); S(); ~S(); };
struct T { ~T(); };
int defined(int x) {
  return user_decl(x) + user_nothrow(x) + user_noexcept(x) + user_throw0(x);
}
int use(const char *a, const char *b, S &s) {
  T t;
  return (int)strlen(a) + (int)strlen(b) + defined(1) + s.m(2);
}
