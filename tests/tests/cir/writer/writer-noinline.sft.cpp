//type:fp
//options:--c++17 --g++ --gnu_version=140000
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// GNU noinline: supp key 14 on every DECL_FUNC issue of the function, from
// the first issue after the front end records the attribute (both spellings),
// and together with the unsupported reason (key 13) in one blob.
[[gnu::noinline]] int s1(int x) { return x + 1; }
int __attribute__((noinline)) s2(int x);
int s3(int x) { return x + 2; }
struct C {
  [[gnu::noinline]] int m(int x) { return x + 3; }
  int n(int x) __attribute__((noinline));
};
int C::n(int x) { return x; }
template<class T> [[gnu::noinline]] T t1(T x) { return x; }
void __attribute__((noinline)) dropped() { __asm__ volatile("nop"); }
int use(C &c) { return s1(1) + s2(2) + s3(3) + c.m(4) + c.n(5) + t1(6); }
