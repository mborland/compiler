//type:fp
//options_all:--c23 --no_il_lowering --il_display
//remark:[GH #?] Registered attributes after the parameter list appertain to the function in C
//require:INCLUDE_EDG_TEST_ATTRIBUTES 1
//require:DO_IL_LOWERING 1
//match_regex:^kind:\s+registered\nnext:\s+NULL\nfamily:\s+std\nsyntactic_location:\s+post func\n(on_primary_declaration:\s+TRUE\n)?name:\s+file-scope other-text@X+: "r_pre"\nnamespace_name:\s+file-scope other-text@X+: "edg"\narguments:\s+file-scope attribute-arg@X+
//match_regex:\A(?![\s\S]*\nkind:\s+registered\nnext:\s+file-scope attribute@X+\nfamily:\s+std\nsyntactic_location:\s+post func\n(on_primary_declaration:\s+TRUE\n)?name:\s+file-scope other-text@X+: "r_pre")
//match_regex:^kind:\s+registered\nnext:\s+file-scope attribute@X+\nfamily:\s+std\nsyntactic_location:\s+post func\n(on_primary_declaration:\s+TRUE\n)?name:\s+file-scope other-text@X+: "r_fn"
//match_regex:^param_ref\.param_num:\s+1\nparam_ref\.levels_up:\s+0
// 10/14/26  [GH #?]
//
// The same as GH-postfunc, in C: the attributes after the parameter list of
// a function declaration are attached to the function, a must-match
// attribute is attached once for any number of identical declarations, and
// accumulating attributes accumulate.

int f(int x) [[edg::r_pre(x > 0)]];
int f(int y) [[edg::r_pre(y > 0)]];
int f(int z) { return z; }

int g(int x) [[edg::r_pre(x > 1)]] { return x; }

int h(void) [[edg::r_fn]];
int h(void) [[edg::r_fn]];
int h(void) { return 1; }
