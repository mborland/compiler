//type:fp
//options_all:--c++20 --instantiate=used --no_il_lowering --il_display
//remark:[GH #?] A registered attribute can name the result of a function
//require:INCLUDE_EDG_TEST_ATTRIBUTES 1
//require:DO_IL_LOWERING 1
//match_regex:type:\s+file-scope type@X+: unsigned short\n(?:[^\n]*\n){0,3}kind:\s+enk_param_ref\nparam_ref.param_num:\s+1\nparam_ref.levels_up:\s+0
//match_regex:type:\s+file-scope type@X+: unsigned char \*\n(?:[^\n]*\n){0,3}kind:\s+enk_param_ref\nparam_ref.param_num:\s+1\nparam_ref.levels_up:\s+0
//match_regex:type:\s+file-scope type@X+: float\n(?:[^\n]*\n){0,3}kind:\s+enk_param_ref\nparam_ref.param_num:\s+1\nparam_ref.levels_up:\s+0
//match_regex:type:\s+file-scope type@X+: long long\n(?:[^\n]*\n){0,3}kind:\s+enk_param_ref\nparam_ref.param_num:\s+1\nparam_ref.levels_up:\s+0
//match_regex:type:\s+file-scope type@X+: signed char\n(?:[^\n]*\n){0,3}kind:\s+enk_param_ref\nparam_ref.param_num:\s+2\nparam_ref.levels_up:\s+1
//match_regex:type:\s+file-scope type@X+: unsigned int\n(?:[^\n]*\n){0,3}kind:\s+enk_param_ref\nparam_ref.param_num:\s+1\nparam_ref.levels_up:\s+1
//match_regex:type:\s+file-scope type@X+: char \*\n(?:[^\n]*\n){0,3}kind:\s+enk_param_ref\nparam_ref.param_num:\s+1\nparam_ref.levels_up:\s+1
//match_regex:type:\s+file-scope type@X+: int &\n(?:[^\n]*\n){0,3}kind:\s+enk_param_ref\nparam_ref.param_num:\s+1\nparam_ref.levels_up:\s+0
//match_regex:type:\s+file-scope type@X+: int &\n(?:[^\n]*\n){0,3}kind:\s+enk_param_ref\nparam_ref.param_num:\s+1\nparam_ref.levels_up:\s+1
//match_regex:type:\s+file-scope type@X+: S \*\n(?:[^\n]*\n){0,3}kind:\s+enk_param_ref\nparam_ref.param_num:\s+0\nparam_ref.levels_up:\s+0
//match_regex:\A(?![\s\S]*\nkind:\s+registered\nnext:\s+file-scope attribute@X+\nfamily:\s+std\nsyntactic_location:\s+post func\n(on_primary_declaration:\s+TRUE\n)?name:\s+file-scope other-text@X+: "r_post")
// 10/14/26  [GH #?]
//
// A registered attribute whose signature starts with "R" ("(R,Xb)" for
// r_post) names the result of the function after the parameter list: the
// first argument is an identifier, which is in scope in the other arguments
// and denotes the result (an object of the type that the function returns, a
// reference if it returns one).  In the IL the result is a parameter
// reference with number 1 and 0 levels up, of the return type, and the
// parameters of the function are 1 level up (this is 0 levels up, as it is
// without a result name): see the recording.  The result name is not part of
// the attribute: the attributes of the redeclarations are equivalent if the
// rest of them is, whatever the result is called and however the
// parameters are called (the regular expression above, which fails if an
// r_post is followed by another, shows that there is one).  The result type
// is substituted in the instances of a template.

// The result of a function, a pointer and a reference.
unsigned short f1(unsigned int x) [[edg::r_post(r, r > x)]];
unsigned short f1(unsigned int y) [[edg::r_post(res, res > y)]] { return 1; }
unsigned char *f2(char *p) [[edg::r_post(r, r == (unsigned char *)p)]];
int &f3(int &x) [[edg::r_post(r, r == x)]];

// A member function, with this.
struct S { float m(int x) [[edg::r_post(r, r > x && this != nullptr)]]; };

// A function template, an instance, and a redeclaration.
template <class T> T h(T x) [[edg::r_post(r, r > x)]];
template <class T> T h(T y) [[edg::r_post(z, z > y)]] { return y; }
long long u1() { return h(1LL); }

// A class template, a member template and a member of an instance.
template <class T> struct C {
  T m(T x) [[edg::r_post(r, r > x)]];
  template <class U> U g(T x, U y) [[edg::r_post(res, res > y && x == x)]];
};
template <class T> T C<T>::m(T x) { return x; }
template <class T> template <class U> U C<T>::g(T x, U y) { return y; }
C<signed char> c;
signed char u2() { return c.m(1) + (signed char)c.g((signed char)1, (signed char)2); }
