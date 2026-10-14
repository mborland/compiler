//type:fn
//options:--clang_version=210000:--gnu_version=150000:--clang_version=210000 -w:--gnu_version=150000 -w
//options_all:--c++20
//remark:[GH #?] Misplaced registered attributes in clang and GNU modes
//require:INCLUDE_EDG_TEST_ATTRIBUTES 1
//match_regex:line 43: error: attribute\s+"edg::r_fn"\s+does\s+not\s+apply\s+here
//match_regex:line 46: error: attribute\s+"edg::r_stmt"\s+does\s+not\s+apply\s+here
//match_regex:line 49: error: attribute\s+"edg::requires"\s+does\s+not\s+apply\s+here
//match_regex:line 52: error: attribute\s+"edg::requires"\s+does\s+not\s+apply\s+here
//match_regex:line 55: error: attributes\s+ignored\s+here\s+because\s+they\s+do\s+not\s+apply\s+to\s+a\s+declared\s+entity
//match_regex:line 58: error: attributes\s+are\s+not\s+allowed\s+here
//match_regex:line 61: error: attribute\s+"edg::r_fn"\s+does\s+not\s+apply\s+here
//match_regex:line 64: error: attribute\s+"edg::r_fn"\s+does\s+not\s+apply\s+here
//match_regex:line 67: error: attributes\s+are\s+not\s+allowed\s+here
//match_regex:line 70: error: attributes\s+are\s+not\s+allowed\s+here
//match_regex:line 74: error: attribute\s+"edg::r_fn"\s+does\s+not\s+apply\s+here
//match_regex:line 75: error: attribute\s+"edg::r_fn"\s+does\s+not\s+apply\s+here
//match_regex:line 76: error: attribute\s+"edg::r_stmt"\s+does\s+not\s+apply\s+here
//match_regex:line 77: error: attribute\s+"edg::r_fn"\s+does\s+not\s+apply\s+here
//match_regex:line 78: error: attribute\s+"edg::r_fn"\s+does\s+not\s+apply\s+here
//match_regex:line 79: error: attribute\s+"edg::r_fn"\s+does\s+not\s+apply\s+here
//match_regex:line 80: error: attribute\s+"edg::r_fn"\s+does\s+not\s+apply\s+here
//match_regex:line 81: error: attribute\s+"edg::r_fn"\s+does\s+not\s+apply\s+here
//match_regex:line 83: error: attribute\s+"edg::r_fn"\s+does\s+not\s+apply\s+here
//match_regex:line 84: error: attribute\s+"edg::r_fn"\s+does\s+not\s+apply\s+here
//match_regex:line 85: error: attribute\s+"edg::r_fn"\s+does\s+not\s+apply\s+here
//match_regex:line 90: error: attribute\s+does\s+not\s+apply\s+to\s+any\s+entity
//match_regex:line 92: error: attribute\s+does\s+not\s+apply\s+to\s+any\s+entity
//match_regex:line 93: error: attribute\s+does\s+not\s+apply\s+to\s+any\s+entity
//match_regex:line 95: error: attribute\s+does\s+not\s+apply\s+to\s+any\s+entity
//match_regex:line 100: error: attribute\s+does\s+not\s+apply\s+to\s+any\s+entity
// 10/14/26  [GH #?]
//
// In clang and GNU modes the front end downgrades the diagnostic for one of
// its own attributes on the wrong kind of entity to a warning, ignores an
// attribute that appertains to nothing, or drops it in some places without a
// diagnostic.  An attribute registered by the embedding program is never
// dropped silently, so it is always an error (and "-w" does not hide it).
// Each regular expression above names one case, at its line.


// A routine attribute on a variable.
[[edg::r_fn]] int v1;

// A statement attribute on a declaration.
[[edg::r_stmt(true)]] int v2;

// An attribute on an empty declaration.
[[edg::requires(true)]];

// An attribute on a class (it appertains to the class, not to an entity).
struct [[edg::requires(true)]] S3 {};

// An attribute on an empty member declaration.
struct S5 { [[edg::r_fn]]; };

// An attribute in front of a linkage specification.
[[edg::r_fn]] extern "C" int f6();

// An attribute on a class template that is declared but not defined.
template <class T> struct [[edg::r_fn]] CT7;

// An attribute on an alias template that is never used.
template <class T> using AL8 [[edg::r_fn]] = T;

// An attribute in front of a static_assert declaration.
[[edg::r_fn]] static_assert(true, "");

// An attribute in front of a static_assert declaration in a block.
void f10() { [[edg::r_stmt(true)]] static_assert(true, ""); }

// An attribute after a type specifier (this and the cases that follow are in
// // the decl-specifiers of a declaration or in a type-id).
int [[edg::r_fn]] v11;
bool [[edg::r_fn]] f12();
void f13(int x) { int [[edg::r_stmt(x > 0)]] y = x; (void)y; }
struct S14 { int [[edg::r_fn]] m; };
void f15(int [[edg::r_fn]] x);
int sc16 = static_cast<int [[edg::r_fn]]>(1);
using T17 = int [[edg::r_fn]];
unsigned long sz18 = sizeof(int [[edg::r_fn]]);
template <class X> struct XX19 {};
XX19<int [[edg::r_fn]]> xx19;
auto tr20() -> int [[edg::r_fn]];
struct CV21 { operator int [[edg::r_fn]] (); };

// Attributes in other places where the front end ignores them in clang and GNU
// // modes.  A registered attribute is never ignored silently: if nothing else
// // diagnoses it, the end of the translation unit does.
template <[[edg::r_fn]] int N22> void tf22();
template <class T> void tf3(T);
template void [[edg::r_fn]] tf3<int>(int);
int *np24 = new int [[edg::r_fn]];
template <class T> void tf4(T);
template [[edg::r_fn]] void tf4<int>(int);

// An inheriting-constructor using-declaration that the front end ignores.
struct A26 { A26(int); };
struct B26 : A26 { using A26::A26; };
struct C26 : B26 { [[edg::r_fn]] using A26::A26; };
