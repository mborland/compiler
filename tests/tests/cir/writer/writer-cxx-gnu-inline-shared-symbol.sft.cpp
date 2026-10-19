//type:fp
//options:--c++17 --g++ --gnu_version=140000 --cir_opt_level=2
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// The shape of glibc's <string.h> in C++ with __OPTIMIZE__: two overloads
// (void * and const void *) that share the asm label "memchr", and the
// always-inline gnu_inline definitions of both.  The two have one symbol and
// so one G-id, and a G-id has one body: the body of the first is streamed
// (inline-only, as for any gnu_inline function), the second leaves it.
typedef unsigned long size_t;
extern "C++" {
extern void *memchr(void *__s, int __c, size_t __n) throw() __asm("memchr");
extern const void *memchr(const void *__s, int __c, size_t __n) throw()
                                                           __asm("memchr");
extern __inline __attribute__((__always_inline__, __gnu_inline__)) void *
memchr(void *__s, int __c, size_t __n) throw()
{ return __builtin_memchr(__s, __c, __n); }
extern __inline __attribute__((__always_inline__, __gnu_inline__)) const void *
memchr(const void *__s, int __c, size_t __n) throw()
{ return __builtin_memchr(__s, __c, __n); }
}
int use(char *p, const char *q) { return memchr(p, 1, 4) == memchr(q, 2, 8); }
