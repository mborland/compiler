//type:fp
//options_all:--c++20 --instantiate=used --no_il_lowering --il_display
//remark:[GH #?] Registered attributes after the parameter list of members of class templates reach the instances
//require:INCLUDE_EDG_TEST_ATTRIBUTES 1
//require:DO_IL_LOWERING 1
//match_regex:type:\s+file-scope type@X+: short\n(?:[^\n]*\n){0,3}kind:\s+enk_param_ref
//match_regex:type:\s+file-scope type@X+: long\n(?:[^\n]*\n){0,3}kind:\s+enk_param_ref
//match_regex:type:\s+file-scope type@X+: unsigned int\n(?:[^\n]*\n){0,3}kind:\s+enk_param_ref
//match_regex:type:\s+file-scope type@X+: unsigned char\n(?:[^\n]*\n){0,3}kind:\s+enk_param_ref
//match_regex:type:\s+file-scope type@X+: unsigned short\n(?:[^\n]*\n){0,3}kind:\s+enk_param_ref
//match_regex:type:\s+file-scope type@X+: unsigned long\n(?:[^\n]*\n){0,3}kind:\s+enk_param_ref
//match_regex:type:\s+file-scope type@X+: unsigned long long\n(?:[^\n]*\n){0,3}kind:\s+enk_param_ref
//match_regex:type:\s+file-scope type@X+: signed char\n(?:[^\n]*\n){0,3}kind:\s+enk_param_ref
//match_regex:type:\s+file-scope type@X+: wchar_t\n(?:[^\n]*\n){0,3}kind:\s+enk_param_ref
//match_regex:type:\s+file-scope type@X+: char16_t\n(?:[^\n]*\n){0,3}kind:\s+enk_param_ref
//match_regex:type:\s+file-scope type@X+: char32_t\n(?:[^\n]*\n){0,3}kind:\s+enk_param_ref
//match_regex:type:\s+file-scope type@X+: float\n(?:[^\n]*\n){0,3}kind:\s+enk_param_ref
//match_regex:\A(?![\s\S]*\nkind:\s+registered\nnext:\s+file-scope attribute@X+\nfamily:\s+std\nsyntactic_location:\s+post func\n(on_primary_declaration:\s+TRUE\n)?name:\s+file-scope other-text@X+: "r_pre")
// 10/14/26  [GH #?]
//
// A contract (r_pre) after the parameter list of a member function, or of a
// member template, of a class template is given to the member of each
// instance of the class, with the template arguments substituted: the
// recording shows each instance with a contract on its parameters of the
// type of the instance, not the template parameter.  This holds where the
// contract is written (in the class or on the definition outside the class),
// and whether the instance of the class exists before or after the
// definition.  The regular expression above fails if an r_pre is followed by
// another attribute, so an instance has only one contract each.

// Contract in the class; the instance is created after the definition.
template <class T> struct S1 { int m(T x) [[edg::r_pre(x > 0)]]; };
template <class T> int S1<T>::m(T x) { return 0; }
int u1() { S1<short> s; return s.m(1); }

// Contract only on the definition outside the class, the class instance
// created first (so the member of the instance exists), then used.
template <class T> struct S2 { int m(T x); };
S2<long> s2;
int u2a() { return s2.m(1); }
template <class T> int S2<T>::m(T x) [[edg::r_pre(x > 0)]] { return 0; }
int u2() { S2<unsigned> s; return s.m(1); }

// Contract defined in the class, with the body.
template <class T> struct S3 { int m(T x) [[edg::r_pre(x > 0)]] { return 0; } };
int u3() { S3<unsigned char> s; return s.m(1); }

// Contract in the class, repeated on the definition after the instance
// exists.
template <class T> struct S4 { int m(T x) [[edg::r_pre(x > 0)]]; };
S4<unsigned short> s4;
int u4a() { return s4.m(1); }
template <class T> int S4<T>::m(T x) [[edg::r_pre(x > 0)]] { return 0; }

// A member template of a class template: contract on the definition.
template <class T> struct S5 { template <class U> int g(T x, U y); };
template <class T> template <class U> int S5<T>::g(T x, U y)
                                            [[edg::r_pre(x > y)]] { return 0; }
int u5() { S5<unsigned long> s; return s.g(1UL, 2.0f); }

// A member template of a class template: contract in the class, and the
// body in the class.
template <class T> struct S6 {
  template <class U> int g(T x, U y) [[edg::r_pre(x > y)]] { return 0; }
};
int u6() { S6<unsigned long long> s; return s.g(1ULL, 2.0L); }

// A member template of a class template: contract in the class, the
// definition outside.
template <class T> struct S7 {
  template <class U> int g(T x, U y) [[edg::r_pre(x > y)]];
};
template <class T> template <class U> int S7<T>::g(T x, U y) { return 0; }
int u7() { S7<signed char> s; return s.g((signed char)1, 2.0); }

// A member of a nested class of a class template.
template <class T> struct O {
  struct I { int m(T x) [[edg::r_pre(x > 0)]]; };
};
int u8() { O<wchar_t>::I i; return i.m(1); }

// A friend function defined in a class template.
template <class T> struct Fr {
  friend int ff(Fr, T x) [[edg::r_pre(x > 0)]] { return 0; }
};
int u9() { return ff(Fr<char16_t>(), 1); }

// A partial specialization of a class template has its own contract.
template <class T, class U> struct P { int m(T x, U y); };
template <class T> struct P<T, int> { int m(T x, int y) [[edg::r_pre(x > y)]]; };
template <class T> int P<T, int>::m(T x, int y) { return 0; }
int u10() { P<char32_t, int> p; return p.m(1, 2); }

// An explicit specialization of a class template has its own contract.
template <class T> struct E { int m(T x); };
template <> struct E<float> { int m(float x) [[edg::r_pre(x > 0)]]; };
int E<float>::m(float x) { return 0; }
int u11() { E<float> e; return e.m(1.0f); }

// An accumulating attribute of the members.
template <class T> struct Ac { int m(T x) [[edg::r_fn]]; };
template <class T> int Ac<T>::m(T x) [[edg::r_fn]] { return 0; }
int u12() { Ac<bool> a; return a.m(true); }
