//type:fp
//options:--c++17 --g++ --gnu_version=140000
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// The GNU used attribute: supp key 11 on every DECL_FUNC issue of the function
// and on DECL_VAR (sticky: the attribute may come with a later declaration),
// in the one blob with noinline (14), in increasing key order (a used function
// is a root: a dropped one is an error, so 11 and 13 never meet).
__attribute__((used)) static int used_static(int x) { return x; }
__attribute__((used)) int used_var = 3;
__attribute__((used)) static int used_static_var = 4;
[[gnu::used]] inline int used_inline(int x) { return x + 1; }
template<class T> __attribute__((used)) T used_template(T x) { return x; }
template int used_template<int>(int);
struct S { __attribute__((used)) static int member; __attribute__((used)) int method(int x) { return x; } };
int S::member = 5;
int later(int x);
int calls_later(int x) { return later(x); }
int later(int x) __attribute__((used));
int later(int x) { return x; }
__attribute__((used, noinline)) int used_noinline(int x) { return x * 2; }
int unused_plain(int x) { return x; }
void use_it() { used_static(1); }
