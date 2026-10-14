//type:fp
//options:--c++20 --instantiate=used:--c++20 --instantiate=used --clang_version=210000:--c++20 --instantiate=used --gnu_version=150000
//remark:[GH #?] Registered attributes after a parameter list that are applied are not reported as dropped
//require:INCLUDE_EDG_TEST_ATTRIBUTES 1
// 10/14/26  [GH #?]
//
// The check at the end of the translation unit that every registered
// attribute was applied or diagnosed has no false positives for the
// attributes after a parameter list: those that are scanned more than once
// (for a template and for each of its instances, in an abbreviated function
// template or in a member of a class template) are applied, and the test has
// no errors (see also GH-reg-account-ok, for attributes in front of a
// declaration).

template <class T> void ok(T) [[edg::r_fn]];
template <class T> void ok(T) [[edg::r_fn]] { }
void use() { ok(1); ok(2.0); }

auto lam = [](auto x) { [[edg::r_stmt(true)]]; };
void use2() { lam(1); }

void abbr(auto x) [[edg::r_fn]];
void abbr(auto x) [[edg::r_fn]] { }
void use3() { abbr(1); abbr(1.0); }

template <class T> struct S {
  void m1(T) [[edg::r_fn]];
  template <class U> void m2(U) [[edg::r_fn]] { }
};
template <class T> void S<T>::m1(T) [[edg::r_fn]] { }
void use4() { S<int> s; s.m1(1); s.m2(1.0); S<char> t; t.m1('a'); t.m2(1); }

// A reference to a parameter before a function parameter pack, or to a
// parameter of a function without a pack, is fine (see
// GH-postfunc-err-after-pack for a parameter after the pack).
template <class... Ts> int before1(int y, Ts... a) [[edg::r_pre(y >= 0)]];
template <class... Ts> int before2(int y, Ts... a) [[edg::r_pre(y >= 0)]] { return y; }
int before3(auto y, auto... a) [[edg::r_pre(y >= 0)]] { return 0; }
template <class... Ts> struct SP { int m(int y, Ts... xs) [[edg::r_pre(y >= 0)]]; };
int use5() { SP<int, long> s; return before2(1, 2, 3L) + before3(1, 2) + s.m(1, 2, 3L); }
