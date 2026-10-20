//type:fp
//options:--c11 --gcc
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
/* INLINE_HINT in C: the functions declared with the inline specifier, static
   or not; not a plain function. */
inline int c99_inline(int x) { return x; }
static inline int static_inline(int x) { return x; }
int plain(int x) { return x; }
int use(int a) { return c99_inline(a) + static_inline(a) + plain(a); }
