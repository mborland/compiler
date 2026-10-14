//type:fn
//options_all:--c++20 --instantiate=used
//remark:[GH #?] A registered attribute that is neither applied nor diagnosed is an error
//require:INCLUDE_EDG_TEST_ATTRIBUTES 1
//cases:5
//match_regex:(line 25: error: attribute\s+does\s+not\s+apply\s+to\s+any\s+entity|line 32: error: attribute\s+does\s+not\s+apply\s+to\s+any\s+entity|line 33: error: attribute\s+does\s+not\s+apply\s+to\s+any\s+entity|line 40: error: attribute\s+does\s+not\s+apply\s+to\s+any\s+entity|line 46: error: attribute\s+does\s+not\s+apply\s+to\s+any\s+entity|line 51: error: attribute\s+does\s+not\s+apply\s+to\s+any\s+entity)
// 10/14/26  [GH #?]
//
// A registered attribute that is neither applied nor diagnosed is an error at
// the end of the translation unit, after the template instantiations.  The
// attributes are told apart by their position in the token stream (not by
// their source position, which the attributes of a macro expansion share),
// and an attribute that is scanned again for an instance of a template is the
// same attribute.  Every case has its own diagnostic; the expression above
// has an alternative for each case, anchored to the line of its error.

#if TEST_NUMBER == 1
// Two attributes from one macro expansion: the first applies to the function,
// the second is dropped (on a using-declaration), so there is an error for
// the second. (The attributes of one expansion have the same source
// position.)
struct A1 { A1(int); };
struct B1 : A1 { };
#define M1 [[edg::r_fn]] void m1(); struct S1 : B1 { [[edg::r_fn]] using A1::A1; };
M1
#elif TEST_NUMBER == 2
// The same with a function-like macro that is used twice: the dropped
// attribute is reported for each use.
struct A2 { A2(int); };
struct B2 : A2 { };
#define M2(n) [[edg::r_fn]] void n(); struct S_##n : B2 { [[edg::r_fn]] using A2::A2; };
M2(m2a)
M2(m2b)
#elif TEST_NUMBER == 3
// An attribute dropped in a local class of a function template that is
// instantiated: one error, not one for the template and one for each
// instance.
struct A3 { A3(int); };
struct B3 : A3 { };
template <class T> void g3() { struct C3 : B3 { [[edg::r_fn]] using A3::A3; }; }
void u3() { g3<int>(); g3<char>(); }
#elif TEST_NUMBER == 4
// The same in a function template that is never instantiated.
struct A4 { A4(int); };
struct B4 : A4 { };
template <class T> void g4() { struct C4 : B4 { [[edg::r_fn]] using A4::A4; }; }
#elif TEST_NUMBER == 5
// An attribute dropped in a member of a class template that is instantiated.
struct A5 { A5(int); };
struct B5 : A5 { };
template <class T> struct W5 { struct C5 : B5 { [[edg::r_fn]] using A5::A5; }; };
W5<int> w5;
#endif
