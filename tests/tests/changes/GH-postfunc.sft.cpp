//type:fp
//options_all:--c++20 --no_il_lowering --il_display
//remark:[GH #?] Registered attributes after the parameter list appertain to the function
//require:INCLUDE_EDG_TEST_ATTRIBUTES 1
//require:DO_IL_LOWERING 1
//match_regex:^kind:\s+registered\nnext:\s+NULL\nfamily:\s+std\nsyntactic_location:\s+post func\n(on_primary_declaration:\s+TRUE\n)?name:\s+file-scope other-text@X+: "r_pre"\nnamespace_name:\s+file-scope other-text@X+: "edg"\narguments:\s+file-scope attribute-arg@X+
//match_regex:\A(?![\s\S]*\nkind:\s+registered\nnext:\s+file-scope attribute@X+\nfamily:\s+std\nsyntactic_location:\s+post func\n(on_primary_declaration:\s+TRUE\n)?name:\s+file-scope other-text@X+: "r_pre")
//match_regex:^kind:\s+registered\nnext:\s+file-scope attribute@X+\nfamily:\s+std\nsyntactic_location:\s+post func\n(on_primary_declaration:\s+TRUE\n)?name:\s+file-scope other-text@X+: "r_fn"
//match_regex:^kind:\s+registered\nnext:\s+NULL\nfamily:\s+std\nsyntactic_location:\s+post func\non_primary_declaration:\s+TRUE\nname:\s+file-scope other-text@X+: "r_fn"
//match_regex:^kind:\s+registered\nnext:\s+NULL\nfamily:\s+std\nsyntactic_location:\s+post func\n(on_primary_declaration:\s+TRUE\n)?name:\s+file-scope other-text@X+: "requires"
//match_regex:^param_ref\.param_num:\s+1\nparam_ref\.levels_up:\s+0
//match_regex:^param_ref\.param_num:\s+0\nparam_ref\.levels_up:\s+0
// 10/14/26  [GH #?]
//
// A registered attribute with the flag RAF_ENTITY_ATTACHED (r_fn, r_pre and
// requires in the edg namespace) written after the parameter list of a
// function declaration is attached to the function, with syntactic location
// "post func" (a standard attribute there would appertain to the function
// type, and be lost when the function is redeclared).  The parameters are in
// scope in its arguments, and are denoted by their position, so a
// redeclaration may rename them.  r_pre must match on every declaration that
// has it (GH-postfunc-err has the errors): an identical list on a later
// declaration is not attached again.  r_fn has no such flag, so the attributes
// accumulate.  The regular expressions above check an attribute with
// arguments in post func position that is alone on its routine and not
// repeated (the second expression fails if any r_pre is followed by another
// attribute), accumulated attributes, and the parameter references (the
// parameter 1 at level 0, and "this" as parameter 0).  The recording shows the
// attributes of every routine.

// Declaration, redeclaration with a renamed parameter, definition (one copy).
int f(int x) [[edg::r_pre(x > 0)]];
int f(int y) [[edg::r_pre(y > 0)]];
int f(int z) { return z; }

// The contract only on the definition.
int g(int x);
int g(int x) [[edg::r_pre(x > 1)]] { return x; }

// The contract only on the declaration (the usual case).
int g2(int x) [[edg::r_pre(x > 1)]];
int g2(int x) { return x; }

// Accumulation: three attributes.
int h() [[edg::r_fn]];
int h() [[edg::r_fn]];
int h() [[edg::r_fn]] { return 1; }

// Members, declared in the class and defined outside it.
struct S {
  int v;
  int get() const [[edg::r_pre(this->v > 0)]];
  int m(int a) [[edg::r_pre(a > v)]];
};
int S::get() const { return v; }
int S::m(int b) [[edg::r_pre(b > v)]] { return b; }

// Function templates: declaration, definition, use.
template <class T> T t1(T a) [[edg::r_pre(a > 0)]];
template <class T> T t1(T b) [[edg::r_pre(b > 0)]] { return b; }
int use() { return t1(1); }

// Abbreviated function template (one copy).
void ab(auto v) [[edg::r_pre(v > 0)]];

// The other flagged attribute.
int k(int x) [[edg::requires(x > 0)]];
