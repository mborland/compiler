//type:fn
//options_all:--c++20 --instantiate=used
//remark:[GH #?] Errors in a registered attribute after the parameter list of a member function defined outside its class
//require:INCLUDE_EDG_TEST_ATTRIBUTES 1
//cases:9
//match_regex:(line 18: error: "this"\s+may\s+only\s+be\s+used\s+inside\s+a\s+nonstatic\s+member\s+function|line 22: error: "this"\s+may\s+only\s+be\s+used\s+inside\s+a\s+nonstatic\s+member\s+function|line 26: error: "this"\s+may\s+only\s+be\s+used\s+inside\s+a\s+nonstatic\s+member\s+function|line 30: error: "this"\s+may\s+only\s+be\s+used\s+inside\s+a\s+nonstatic\s+member\s+function|line 34: error: "this"\s+may\s+only\s+be\s+used\s+inside\s+a\s+nonstatic\s+member\s+function|line 38: error: attribute\s+"edg::r_post"\s+cannot\s+name\s+the\s+result\s+of\s+a\s+function\s+that\s+returns\s+void|line 43: error: attribute\s+"edg::r_post"\s+does\s+not\s+match\s+the\s+attributes\s+of\s+an\s+earlier\s+declaration|line 48: error: cannot\s+capture\s+"this"|line 52: error: cannot\s+capture\s+"this")
// 10/15/26  [GH #?]
//
// Errors in a registered attribute after the parameter list of a member
// function defined outside its class (see GH-result-name-member-cases for the
// cases that are fine).  Every case has its own diagnostic; the expression above
// has an alternative for each case, anchored to the line of its error.

#if TEST_NUMBER == 1
// A static member function uses a non-static member: an error (with a
// contract that can name the result and one that cannot), not a crash.
struct C1 { int val; static int g(); };
int C1::g() [[edg::r_post(val >= 0)]] { return 0; }
#elif TEST_NUMBER == 2
// The same with r_pre.
struct C2 { int val; static int g(); };
int C2::g() [[edg::r_pre(val >= 0)]] { return 0; }
#elif TEST_NUMBER == 3
// The same with this->.
struct C3 { int val; static int g(); };
int C3::g() [[edg::r_post(r, this->val > 0)]] { return 0; }
#elif TEST_NUMBER == 4
// The same with a parameter and a member.
struct C4 { int val; static int g(int x); };
int C4::g(int x) [[edg::r_pre(x > val)]] { return 0; }
#elif TEST_NUMBER == 5
// A static member function of a class template.
template <class T> struct C5 { T val; static int g(); };
template <class T> int C5<T>::g() [[edg::r_pre(val >= 0)]] { return 0; }
#elif TEST_NUMBER == 6
// A name for the result of a void member defined outside the class.
struct C6 { int val; void set(int v); };
void C6::set(int w) [[edg::r_post(r, val == w)]] { val = w; }
#elif TEST_NUMBER == 7
// A member function defined outside the class with a contract that differs
// from the one in the class.
struct C7 { int val; int a() [[edg::r_post(val >= 0)]]; };
int C7::a() [[edg::r_post(val >= 1)]] { return val; }
#elif TEST_NUMBER == 8
// A lambda that captures "this" in a contract of a member function is an
// error (it used to be an internal error).
struct C8 { int val; int bump(); };
int C8::bump() [[edg::r_pre([this] { return val; }() >= 0)]] { return val; }
#elif TEST_NUMBER == 9
// The same for a const member function and r_post.
struct C9 { int val; int bump() const; };
int C9::bump() const [[edg::r_post([*this] { return val; }() >= 0)]] { return val; }
#endif
