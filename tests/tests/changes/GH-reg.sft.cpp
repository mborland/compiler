//type:fp
//options_all:--c++20 --no_il_lowering --il_display
//remark:[GH #?] Attributes registered by the embedding program
//require:INCLUDE_EDG_TEST_ATTRIBUTES 1
//require:DO_IL_LOWERING 1
//match_regex:^kind:\s+registered\nnext:\s+file-scope attribute@X+\nfamily:\s+std\nsyntactic_location:\s+prefix\nname:\s+file-scope other-text@X+: "r_fn"\nnamespace_name:\s+file-scope other-text@X+: "edg"
//match_regex:^kind:\s+registered\nnext:\s+NULL\nfamily:\s+std\nsyntactic_location:\s+prefix\nname:\s+file-scope other-text@X+: "requires"\nnamespace_name:\s+file-scope other-text@X+: "edg"\narguments:\s+file-scope attribute-arg@X+
//match_regex:^file-scope attribute-arg@X+\nkind:\s+expression\nnext:\s+NULL(\n.+?)+?\nexpr:\s+file-scope expr-node@X+
//match_regex:^operation\.kind:\s+eok_gt\noperation\.type_kind:\s+tk_integer
//match_regex:^constant:\s+file-scope constant@X+: sizeof\(T#\(1,1\)\)
//match_regex:^constant:\s+file-scope constant@X+: 4UL$
//match_regex:^kind:\s+enk_sizeof_pack$
//match_regex:^constant:\s+file-scope constant@X+: sizeof\(Ts#\(1,1\)\)
//match_regex:^constant:\s+file-scope constant@X+: 5$
//match_regex:^constant:\s+file-scope constant@X+: 7$
//match_regex:^constant:\s+file-scope constant@X+: 5UL$
//match_regex:^constant:\s+file-scope constant@X+: 3UL$
//match_regex:^operation\.kind:\s+eok_land\noperation\.type_kind:\s+tk_integer
// 10/14/26  [GH #?]
//
// An attribute registered by the program embedding the front end is scanned
// like an attribute the front end defines: its arguments are typed
// expressions (not raw tokens), and it is recorded with kind "registered".
// The attributes of the "edg" namespace used here are registered by the
// front end itself when it is built with INCLUDE_EDG_TEST_ATTRIBUTES.
//
// The regular expressions above check, in this order: a prefix attribute
// without arguments, on a declaration and on a definition; a prefix attribute
// with a typed expression argument (a bool, comparing a global); and, for the
// attribute of a function template, an argument that is dependent in the
// prototype and substituted in the instance.
//
// The attributes on statements and in a template body do not appear in the
// IL display; they must only be accepted without a diagnostic.

[[edg::r_fn]] int g(int);
[[edg::r_fn]] int g(int a) { return a; }

extern int gv;
[[edg::requires(gv > 0)]] void h();

template <class T> [[edg::requires(sizeof(T) > 1)]] void tf(T) {}
void use() { tf(1); }

// A type pack (sizeof...(Ts), and a fold over Ts) and a non-type pack are fine
// in a declaration; the instances have the packs substituted (not empty).
template <class... Ts> [[edg::requires(sizeof...(Ts) > 0)]] void tp(Ts...);
template <class... Ts> [[edg::requires((sizeof(Ts) > 0 && ...))]] void tf2(Ts...);
template <int... Ns> [[edg::requires(((Ns > 0) && ...))]] void tn();
template <int... Ns> [[edg::requires(sizeof...(Ns) == 3)]] void tn3();
void use3() { tp(1, 2, 3, 4, 5); tf2(short(1), char(2)); tn<5, 7>(); tn3<1, 2, 3>(); }

void f(int n) {
  [[edg::r_stmt(n > 0)]];
  [[using edg: r_stmt(n > 0)]];
  [[edg::r_expr(n + 1)]];
  [[edg::r_expr(n)]];
}

template <class T, class... Ts> void tb(T v, Ts... args) {
  [[edg::r_stmt(v > 0)]];
  [[edg::r_stmt(sizeof...(Ts) == 2)]];
  [[edg::r_stmt(sizeof...(args) == 2)]];
  [[edg::r_stmt(((args > 0) && ...))]];
}
void use2() { tb(1, 2, 3); }

// A registered attribute can be tested for.
#if __has_cpp_attribute(edg::r_fn) != 1
#error "edg::r_fn is not reported as supported"
#endif
#if __has_cpp_attribute(edg::requires) != 1
#error "edg::requires is not reported as supported"
#endif
#if __has_cpp_attribute(edg::nope)
#error "edg::nope is reported as supported"
#endif
