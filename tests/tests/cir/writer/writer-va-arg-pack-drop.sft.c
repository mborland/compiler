//type:fp
//options:--c11 --gcc
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// A gnu_inline wrapper that uses __builtin_va_arg_pack (glibc's
// _FORTIFY_SOURCE wrappers of printf, sprintf, open ...) cannot be written
// and is dropped; it stays an inline-only function: every DECL_FUNC of it,
// the one that drops the definition included, has INLINE_ONLY (0x10), so that
// the consumer makes it a plain declaration and not an external dropped
// definition (an error even when nothing refers to it).
extern int __builtin_sprintf(char *, const char *, ...);
extern __inline __attribute__((gnu_inline, always_inline, artificial)) int
wrap_sprintf(char *buf, const char *fmt, ...) {
  return __builtin_sprintf(buf, fmt, __builtin_va_arg_pack());
}
extern __inline __attribute__((gnu_inline, always_inline, artificial)) int
wrap_len(int fd, const char *path, ...) {
  return __builtin_va_arg_pack_len() + fd + (path != 0);
}
int use(char *b, int x) { return wrap_sprintf(b, "%d", x) + wrap_len(1, b, x); }
