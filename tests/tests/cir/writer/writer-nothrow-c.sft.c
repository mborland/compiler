//type:fp
//options:--c11 --gnu_version=150200
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// DECL_FUNC flag NOTHROW (128) in a C stream (M5.1 W1): a definition, a
// function with the nothrow attribute and the library functions that clang's
// table says do not throw (strlen; not memcmp or printf) have it; a
// declaration of a function that the user writes does not (clangir gives it
// no nounwind).
typedef unsigned long size_t;
extern int user_decl(int);
extern int user_nothrow(int) __attribute__((nothrow));
extern int memcmp(const void *, const void *, size_t);
extern size_t strlen(const char *);
extern int printf(const char *, ...);
int defined(int x) { return user_decl(x) + user_nothrow(x); }
int use(const char *a, const char *b) {
  return memcmp(a, b, strlen(a)) + printf("%d", defined(1));
}
