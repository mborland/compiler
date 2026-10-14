//type:fp
//options:--c++20 --instantiate=used:--c++20 --instantiate=used --clang_version=210000:--c++20 --instantiate=used --gnu_version=150000
//remark:[GH #?] Registered attributes that are applied are not reported as dropped
//require:INCLUDE_EDG_TEST_ATTRIBUTES 1
// 10/14/26  [GH #?]
//
// The check at the end of the translation unit that every registered
// attribute was applied or diagnosed has no false positives: attributes that
// are scanned more than once (for the prototype of a template and for each of
// its instances, by a macro that is used more than once, in a lambda or an
// abbreviated function template) are applied, and the test has no errors.

struct B { };

#define M1 [[edg::r_fn]] void m1(); [[edg::r_fn]] void m1b();
M1
#define M2(n) [[edg::r_fn]] void n(); [[edg::r_fn]] void n##b();
M2(m2a)
M2(m2b)
struct C : B { M1 };

template <class T> void g() {
  struct L { [[edg::r_fn]] void m(); };
  [[edg::r_stmt(true)]];
}
void h() { g<int>(); g<char>(); }
template <class T> void g_unused() { struct L2 { [[edg::r_fn]] void m(); }; }

template <class T> [[edg::r_fn]] void ok(T);
template <class T> [[edg::r_fn]] void ok(T) { }
void use() { ok(1); ok(2.0); }

auto lam = [](auto x) { [[edg::r_stmt(true)]]; };
void use2() { lam(1); }

[[edg::r_fn]] void abbr(auto x);
[[edg::r_fn]] void abbr(auto x) { }
void use3() { abbr(1); abbr(1.0); }

template <class T> struct S {
  [[edg::r_fn]] void m1(T);
  template <class U> [[edg::r_fn]] void m2(U) { }
};
template <class T> [[edg::r_fn]] void S<T>::m1(T) { }
void use4() { S<int> s; s.m1(1); s.m2(1.0); S<char> t; t.m1('a'); t.m2(1); }
