//type:fp
//options_all:--c++20 --no_il_lowering --il_display
//remark:[GH #?] Registered attributes of a function template reach the instances that exist
//require:INCLUDE_EDG_TEST_ATTRIBUTES 1
//require:DO_IL_LOWERING 1
//match_regex:^kind:\s+registered\nnext:\s+NULL\nfamily:\s+std\nsyntactic_location:\s+post func\non_primary_declaration:\s+TRUE\nname:\s+file-scope other-text@X+: "r_pre"
//match_regex:\A(?![\s\S]*\nkind:\s+registered\nnext:\s+file-scope attribute@X+\nfamily:\s+std\nsyntactic_location:\s+post func\n(on_primary_declaration:\s+TRUE\n)?name:\s+file-scope other-text@X+: "r_pre")
// 10/14/26  [GH #?]
//
// The attributes of a function template are copied (with the template
// arguments substituted) to each instance when it is created.  An instance
// that exists before an attribute is attached to the template did not get it.
// A registered attribute attached later, by a redeclaration or the
// definition, now also goes to the instances that exist, including an
// instance that has no definition yet.  The contract of the first
// declaration that has one is attached once, so each instance has exactly one
// r_pre (see the second regular expression above, which fails if one r_pre
// is followed by another attribute, and the recording).

// An instance with no definition (created by an unevaluated use) exists
// before the definition that has the contract.
template <class T> T k(T v);
decltype(k(1)) unevaluated_use = 0;
template <class T> T k(T v) [[edg::r_pre(v > 1)]] { return v; }

// An instance exists before a redeclaration has the contract; the
// definition and a later redeclaration repeat it (not attached again).
template <class T> T f(T v);
int use1() { return sizeof(f(1)); }
template <class T> T f(T v) [[edg::r_pre(v > 2)]];
template <class T> T f(T v) [[edg::r_pre(v > 2)]] { return v; }
template <class T> T f(T v) [[edg::r_pre(v > 2)]];
long use2() { return sizeof(f(2L)); }

// An accumulating attribute is on every declaration, and goes to the
// instance that exists when a later declaration has it.
template <class T> T fa(T v) [[edg::r_fn]];
int use3() { return sizeof(fa(1)); }
template <class T> T fa(T v) [[edg::r_fn]];
template <class T> T fa(T v) [[edg::r_fn]] { return v; }
template <class T> T fa(T v) [[edg::r_fn]];
