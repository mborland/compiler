//type:fp
//options:--c11 --gcc
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// Supp keys 16, 17 and 18 in C: always_inline, cold and hot, sticky across
// declarations, in increasing key order.
static inline __attribute__((always_inline)) int ai(int x) { return x; }
__attribute__((cold)) int cold_fn(int x) { return x; }
__attribute__((hot)) int hot_fn(int x) { return x; }
int later(int x);
int caller(int x) { return later(x); }
int later(int x) __attribute__((hot));
int later(int x) { return x; }
int plain(int x) { return x; }
int use(int x) { return ai(x) + cold_fn(x) + hot_fn(x) + caller(x) + plain(x); }
