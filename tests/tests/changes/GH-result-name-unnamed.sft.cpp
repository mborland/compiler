//type:fp
//options_all:--c++20 --instantiate=used --no_il_lowering --il_display
//remark:[GH #?] A registered attribute that can name the result has no name for it when it has no identifier and comma first
//require:INCLUDE_EDG_TEST_ATTRIBUTES 1
//require:DO_IL_LOWERING 1
//match_regex:type:\s+file-scope type@X+: short\n(?:[^\n]*\n){0,3}kind:\s+enk_param_ref\nparam_ref.param_num:\s+1\nparam_ref.levels_up:\s+1
//match_regex:type:\s+file-scope type@X+: unsigned short\n(?:[^\n]*\n){0,3}kind:\s+enk_param_ref\nparam_ref.param_num:\s+1\nparam_ref.levels_up:\s+1
//match_regex:type:\s+file-scope type@X+: signed char\n(?:[^\n]*\n){0,3}kind:\s+enk_param_ref\nparam_ref.param_num:\s+1\nparam_ref.levels_up:\s+1
//match_regex:type:\s+file-scope type@X+: unsigned char\n(?:[^\n]*\n){0,3}kind:\s+enk_param_ref\nparam_ref.param_num:\s+1\nparam_ref.levels_up:\s+1
//match_regex:type:\s+file-scope type@X+: wchar_t\n(?:[^\n]*\n){0,3}kind:\s+enk_param_ref\nparam_ref.param_num:\s+1\nparam_ref.levels_up:\s+1
//match_regex:type:\s+file-scope type@X+: char16_t\n(?:[^\n]*\n){0,3}kind:\s+enk_param_ref\nparam_ref.param_num:\s+1\nparam_ref.levels_up:\s+1
//match_regex:type:\s+file-scope type@X+: char32_t\n(?:[^\n]*\n){0,3}kind:\s+enk_param_ref\nparam_ref.param_num:\s+1\nparam_ref.levels_up:\s+1
//match_regex:type:\s+file-scope type@X+: D \*\n(?:[^\n]*\n){0,3}kind:\s+enk_param_ref\nparam_ref.param_num:\s+1\nparam_ref.levels_up:\s+1
//match_regex:\A(?![\s\S]*\nkind:\s+registered\nnext:\s+file-scope attribute@X+\nfamily:\s+std\nsyntactic_location:\s+post func\n(on_primary_declaration:\s+TRUE\n)?name:\s+file-scope other-text@X+: "r_post")
// 10/15/26  [GH #?]
//
// The name of the result is optional in the signature "(R,Xb)": the arguments
// start with a name only when they start with an identifier and a comma.
// Without one the attribute is allowed wherever it can be attached to a
// function, including where a name is an error (a function that returns void,
// a constructor, a destructor, a conversion function, a deduced or trailing
// return type), the arguments are a condition, and the parameters of the
// function are still one level up in the IL (as with a name; see
// GH-result-name).  An identifier alone (not followed by a comma) or one that
// starts an expression is a condition, not a name, also when it is the name of
// a parameter.

void f1(short x) [[edg::r_post(x > 0)]];
struct C {
  C(unsigned short x) [[edg::r_post(x > 0)]];
  ~C() [[edg::r_post(true)]];
  operator long long() [[edg::r_post(true)]];
};
auto a1(signed char x) [[edg::r_post(x > 0)]] { return x; }
decltype(auto) a2(unsigned char x) [[edg::r_post(x > 0)]] { return x; }
auto a3(wchar_t x) [[edg::r_post(x > 0)]] -> int;
int f2(char16_t x) [[edg::r_post(x < 1000)]];
int f3(char32_t x) [[edg::r_post(x)]];
struct D { int val; };
int f4(D *c) [[edg::r_post(c->val > 0)]];
