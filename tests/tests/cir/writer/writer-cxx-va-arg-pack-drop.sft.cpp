//type:fp
//options:--c++17 --g++ --gnu_version=140000
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// As writer-va-arg-pack-drop in C++ (a gnu_inline function is always
// inline-only in GNU C++ mode): the dropped definition keeps INLINE_ONLY.
extern "C" int __builtin_sprintf(char *, const char *, ...) throw();
extern "C" __inline __attribute__((gnu_inline, always_inline, artificial)) int
wrap_sprintf(char *buf, const char *fmt, ...) {
  return __builtin_sprintf(buf, fmt, __builtin_va_arg_pack());
}
int use(char *b, int x) { return wrap_sprintf(b, "%d", x); }
