//type:fp
//options_all:--c++20 --instantiate=used --no_il_lowering --il_display
//remark:[GH #?] Members of the instances of a class template have the contract that is only on a definition
//require:INCLUDE_EDG_TEST_ATTRIBUTES 1
//require:DO_IL_LOWERING 1
//match_regex:type:\s+file-scope type@X+: short\n(?:[^\n]*\n){0,3}kind:\s+enk_param_ref\nparam_ref.param_num:\s+1\nparam_ref.levels_up:\s+0
//match_regex:type:\s+file-scope type@X+: unsigned short\n(?:[^\n]*\n){0,3}kind:\s+enk_param_ref\nparam_ref.param_num:\s+1\nparam_ref.levels_up:\s+0
//match_regex:type:\s+file-scope type@X+: signed char\n(?:[^\n]*\n){0,3}kind:\s+enk_param_ref\nparam_ref.param_num:\s+1\nparam_ref.levels_up:\s+0
//match_regex:type:\s+file-scope type@X+: unsigned char\n(?:[^\n]*\n){0,3}kind:\s+enk_param_ref\nparam_ref.param_num:\s+1\nparam_ref.levels_up:\s+0
//match_regex:type:\s+file-scope type@X+: wchar_t\n(?:[^\n]*\n){0,3}kind:\s+enk_param_ref\nparam_ref.param_num:\s+1\nparam_ref.levels_up:\s+0
//match_regex:\A(?![\s\S]*\nkind:\s+registered\nnext:\s+file-scope attribute@X+\nfamily:\s+std\nsyntactic_location:\s+post func\n(on_primary_declaration:\s+TRUE\n)?name:\s+file-scope other-text@X+: "r_pre")
// 10/14/26  [GH #?]
//
// The contract of a member of a class template is the one on any declaration
// of the member, including a definition outside the class, and it reaches the
// member of every instance of the class whenever the instance is created, also
// when the definition of the member is never instantiated: a member of an
// instance that is only declared (explicit instantiation declaration), and
// the explicit specialization of a member, which has the contract of the
// template when it has none of its own.  See the recording: each of these
// members has exactly one r_pre (the regular expression above, which fails if
// an r_pre is followed by another attribute) with the parameter of the type of
// the instance.

// An explicit instantiation declaration: the member of the instance is
// never defined or instantiated.
template <class T> struct A { int m(T x); };
template <class T> int A<T>::m(T x) [[edg::r_pre(x > 0)]] { return 0; }
extern template struct A<short>;
int ua() { A<short> a; return a.m(1); }

// An explicit specialization of the member without a contract.
template <class T> struct B { int m(T x); };
template <class T> int B<T>::m(T x) [[edg::r_pre(x > 0)]] { return 0; }
template <> int B<unsigned short>::m(unsigned short x) { return 1; }

// An explicit specialization that repeats the contract.
template <class T> struct C { int m(T x); };
template <class T> int C<T>::m(T x) [[edg::r_pre(x > 0)]] { return 0; }
template <> int C<signed char>::m(signed char x) [[edg::r_pre(x > 0)]] { return 1; }

// The instance of the class exists before the definition, and its member is
// used before it.
template <class T> struct D { int m(T x); };
D<unsigned char> d;
int ud() { return d.m(1); }
template <class T> int D<T>::m(T x) [[edg::r_pre(x > 0)]] { return 0; }

// A member template of the instance, with the specialization of its
// instance declared after the definition and repeating it.
template <class T> struct E { template <class U> int g(U y); };
E<wchar_t> e;
template <class T> template <class U> int E<T>::g(U y) [[edg::r_pre(y > 0)]] { return 0; }
int ue() { return e.g((wchar_t)1); }
