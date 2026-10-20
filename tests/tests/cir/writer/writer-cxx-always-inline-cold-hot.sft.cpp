//type:fp
//options:--c++17 --g++ --gnu_version=140000
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// Supp keys 16 (always_inline), 17 (cold) and 18 (hot) on DECL_FUNC: the GNU
// attributes in both spellings, on members, templates and instances; sticky
// (a later declaration may add them); in the blob in increasing key order
// with noinline (14). always_inline without the inline specifier is ignored
// by the front end (as gcc 4.7 and later do not mark the routine inline) and
// not sent.
__attribute__((always_inline)) inline int ai(int x) { return x; }
[[gnu::always_inline]] inline int ai2(int x) { return x; }
__attribute__((cold)) int cold_fn(int x) { return x; }
[[gnu::cold]] int cold2(int x) { return x; }
__attribute__((hot)) int hot_fn(int x) { return x; }
[[gnu::hot]] int hot2(int x) { return x; }
int later(int x);
int caller(int x) { return later(x); }
int later(int x) __attribute__((cold));
inline int later2(int x);
int caller2(int x) { return later2(x); }
inline int later2(int x) __attribute__((always_inline));
inline int later2(int x) { return x; }
int later(int x) { return x; }
int plain(int x) { return x; }
__attribute__((always_inline)) int not_inline(int x) { return x; }
struct S {
  __attribute__((always_inline)) int m(int x) { return x; }
  __attribute__((cold)) int c(int x);
  [[gnu::hot]] static int h(int x) { return x; }
};
int S::c(int x) { return x; }
template<class T> __attribute__((always_inline)) inline T tai(T x) { return x; }
template<class T> __attribute__((cold)) T tcold(T x) { return x; }
template<class T> struct C {
  __attribute__((hot)) T m(T x) { return x; }
  __attribute__((cold)) T o(T x);
};
template<class T> T C<T>::o(T x) { return x; }
__attribute__((cold, hot, always_inline, noinline)) inline int all(int x) {
  return x;
}
int use(S &s) {
  return ai(1) + ai2(1) + cold_fn(1) + cold2(1) + hot_fn(1) + hot2(1) +
         caller(1) + caller2(1) + plain(1) + not_inline(1) + s.m(1) + s.c(1) +
         S::h(1) + tai(1) + tcold(1) + C<int>().m(1) + C<int>().o(1) + all(1);
}
