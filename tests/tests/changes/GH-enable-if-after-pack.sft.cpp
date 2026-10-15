//type:fn
//options_all:--clang_v 180000
//options:--c++11:--c++14:--c++17:--c++20:--c++23
//remark:[GH #?] A parameter after a function parameter pack, named in an enable_if condition
//match_regex:line 67: error: no\ instance\ of\ function\ template\ "last_pos"\ matches\ the\ argument\ list
//match_regex:line 69: error: no\ instance\ of\ function\ template\ "last_pos"\ matches\ the\ argument\ list
//match_regex:line 71: error: no\ instance\ of\ function\ template\ "last_pos"\ matches\ the\ argument\ list
//match_regex:line 74: error: no\ instance\ of\ function\ template\ "last_pos"\ matches\ the\ argument\ list
//match_regex:line 77: error: no\ instance\ of\ function\ template\ "last_pos"\ matches\ the\ argument\ list
//match_regex:line 79: error: no\ instance\ of\ function\ template\ "last_pos"\ matches\ the\ argument\ list
//match_regex:line 85: error: no\ instance\ of\ function\ template\ "mid"\ matches\ the\ argument\ list
//match_regex:line 86: error: no\ instance\ of\ function\ template\ "mid"\ matches\ the\ argument\ list
//match_regex:line 88: error: no\ instance\ of\ function\ template\ "mid"\ matches\ the\ argument\ list
//match_regex:line 89: error: no\ instance\ of\ function\ template\ "mid"\ matches\ the\ argument\ list
//match_regex:line 91: error: no\ instance\ of\ function\ template\ "mid"\ matches\ the\ argument\ list
//match_regex:line 92: error: no\ instance\ of\ function\ template\ "mid"\ matches\ the\ argument\ list
//match_regex:line 98: error: no\ instance\ of\ function\ template\ "below"\ matches\ the\ argument\ list
//match_regex:line 104: error: no\ instance\ of\ function\ template\ "only_x"\ matches\ the\ argument\ list
//match_regex:line 110: error: no\ instance\ of\ function\ template\ "between"\ matches\ the\ argument\ list
//match_regex:line 112: error: no\ instance\ of\ function\ template\ "between"\ matches\ the\ argument\ list
//match_regex:line 114: error: no\ instance\ of\ function\ template\ "between"\ matches\ the\ argument\ list
//match_regex:line 116: error: no\ instance\ of\ function\ template\ "between"\ matches\ the\ argument\ list
//match_regex:line 135: error: no\ instance\ of\ function\ template\ "S::m"\ matches\ the\ argument\ list
//match_regex:line 137: error: no\ instance\ of\ function\ template\ "S::cm"\ matches\ the\ argument\ list
//match_regex:line 139: error: no\ instance\ of\ function\ template\ "S::sm"\ matches\ the\ argument\ list
//match_regex:line 149: error: no\ instance\ of\ function\ template\ "W<T>::m\ \[with\ T=int\]"\ matches\ the\ argument\ list
//match_regex:line 161: error: function\ "C<Cs\.\.\.>::k\ \[with\ Cs=<int,\ int>\]"\ cannot\ be\ called\ with\ the\ given\ argument\ list
//match_regex:line 164: error: function\ "C<Cs\.\.\.>::sk\ \[with\ Cs=<int,\ int>\]"\ cannot\ be\ called\ with\ the\ given\ argument\ list
//match_regex:line 167: error: no\ instance\ of\ function\ template\ "C<Cs\.\.\.>::mt\ \[with\ Cs=<int,\ int>\]"\ matches\ the\ argument\ list
//match_regex:line 172: error: function\ "C<Cs\.\.\.>::k\ \[with\ Cs=<>\]"\ cannot\ be\ called\ with\ the\ given\ argument\ list
//match_regex:line 174: error: function\ "C<Cs\.\.\.>::sk\ \[with\ Cs=<>\]"\ cannot\ be\ called\ with\ the\ given\ argument\ list
//match_regex:line 176: error: no\ instance\ of\ function\ template\ "C<Cs\.\.\.>::mt\ \[with\ Cs=<>\]"\ matches\ the\ argument\ list
//match_regex:line 193: error: no\ instance\ of\ function\ template\ "P<Cs\.\.\.>::k\ \[with\ Cs=<int,\ int>\]"\ matches\ the\ argument\ list
//match_regex:line 194: error: no\ instance\ of\ function\ template\ "P<Cs\.\.\.>::k\ \[with\ Cs=<int,\ int>\]"\ matches\ the\ argument\ list
//match_regex:line 196: error: no\ instance\ of\ function\ template\ "P<Cs\.\.\.>::s\ \[with\ Cs=<int,\ int>\]"\ matches\ the\ argument\ list
//match_regex:line 208: error: no\ instance\ of\ function\ template\ "Q<Cs\.\.\.>::k\ \[with\ Cs=<int,\ int>\]"\ matches\ the\ argument\ list
//match_regex:line 209: error: no\ instance\ of\ function\ template\ "Q<Cs\.\.\.>::k\ \[with\ Cs=<int,\ int>\]"\ matches\ the\ argument\ list
//match_regex:line 211: error: no\ instance\ of\ function\ template\ "Q<Cs\.\.\.>::k\ \[with\ Cs=<int,\ int>\]"\ matches\ the\ argument\ list
//match_regex:line 218: error: no\ instance\ of\ function\ template\ "ref"\ matches\ the\ argument\ list
//match_regex:\A(?![\s\S]*line (?!(?:67|69|71|74|77|79|85|86|88|89|91|92|98|104|110|112|114|116|135|137|139|149|161|164|167|172|174|176|193|194|196|208|209|211|218)\b)\d+: error)
// 10/15/26  [GH #?]
//
// A parameter that follows a function parameter pack, named in an enable_if
// condition on a function template, is the wrong parameter in an instance.
//
// The condition keeps the parameter numbering of the template, in which the
// pack is one parameter, while the instance's pack is expanded to its
// elements, so a call is enabled or disabled by testing an element of the pack
// (or, for an empty pack, no argument at all) instead of the named parameter.
// Previously, whether a call of these templates was accepted depended on the
// number of elements of the pack: for most lengths, calls were rejected when
// their conditions held and accepted when they did not.  The same was true for
// the members of a class template with a function parameter pack, which are
// not templates themselves, whether the pack is empty or not.  A reference
// parameter after the pack could stop the front end (see below).
//
// This is now fixed, and the references to the parameters of the requires-
// expressions and lambdas in a condition (see GH-enable-if-after-pack-
// requires) are not touched.  Each call below is on its own line and is marked
// "accepted" or "rejected", which is what Clang does with it.  The expected
// output has one error, on that line, for each call marked "rejected" (a
// match_regex each), and none on any other line (the last match_regex).
// y follows the pack xs; pack length 0..3, explicit template arguments or deduction.
template <class... Ts>
int last_pos(Ts... xs, int y) __attribute__((enable_if(y > 0, "y > 0"))) { return y; }
int a0 = last_pos<>(3);                         // empty pack: accepted
int a1 = last_pos<>(-3);                        // empty pack: rejected
int a2 = last_pos(3);                           // deduced empty pack: accepted
int a3 = last_pos(-3);                          // deduced empty pack: rejected
int b0 = last_pos<int>(5, 3);                   // one element: accepted
int b1 = last_pos<int>(5, -3);                  // one element, y negative: rejected
int b2 = last_pos<int>(-5, 3);                  // one element negative, y positive: accepted
int c0 = last_pos<int, int>(5, -6, 3);          // two elements, y positive: accepted
int c1 = last_pos<int, int>(5, 6, -3);          // two elements, y negative: rejected
int c2 = last_pos<int, int>(-5, -6, 3);         // two elements negative, y positive: accepted
int d0 = last_pos<int, int, int>(-1, -2, -3, 4);  // three elements, y positive: accepted
int d1 = last_pos<int, int, int>(1, 2, 3, -4);    // three elements, y negative: rejected
int d2 = last_pos<char, double>('a', -2.5, 7);    // elements of other types: accepted
int d3 = last_pos<char, double>('a', 2.5, -7);    // elements of other types, y negative: rejected

// A parameter before the pack as well: x is unaffected by the pack, y is.
template <class... Ts>
int mid(int x, Ts... xs, int y) __attribute__((enable_if(x > 0 && y > 0, "x > 0 && y > 0"))) { return x + y; }
int e0 = mid<>(1, 2);                           // accepted
int e1 = mid<>(-1, 2);                          // x negative: rejected
int e2 = mid<>(1, -2);                          // y negative: rejected
int e3 = mid<int>(1, 100, 2);                   // accepted
int e4 = mid<int>(-1, 100, 2);                  // x negative: rejected
int e5 = mid<int>(1, 100, -2);                  // y negative: rejected
int e6 = mid<int, int, int>(1, -7, -8, -9, 2);  // negative elements only: accepted
int e7 = mid<int, int, int>(1, -7, -8, -9, -2); // y negative: rejected
int e8 = mid<int, int, int>(-1, 7, 8, 9, 2);    // x negative: rejected

// Both x and y in one comparison, with elements that would give the wrong answer.
template <class... Ts>
int below(int x, Ts... xs, int y) __attribute__((enable_if(y > x, "y > x"))) { return y; }
int f0 = below<int, int>(5, 0, 0, 9);           // y > x, elements 0: accepted
int f1 = below<int, int>(5, 9, 9, 2);           // y < x, elements 9: rejected

// A condition that does not name y, on a template that has a parameter after the pack.
template <class... Ts>
int only_x(int x, Ts... xs, int y) __attribute__((enable_if(x > 0, "x > 0"))) { return x + y; }
int g0 = only_x<int, int>(1, -7, -8, -9);       // accepted
int g1 = only_x<int, int>(-1, 7, 8, 9);         // rejected

// Two packs with a parameter between them: only the non-pack parameter is named.
template <class... Ts, class... Us>
int between(Ts... a, int m, Us... b) __attribute__((enable_if(m > 0, "m > 0"))) { return m; }
int h0 = between<>(1, 2, 3);                    // empty first pack: accepted
int h1 = between<>(-1, 2, 3);                   // empty first pack: rejected
int h2 = between<int>(9, 1);                    // empty second pack: accepted
int h3 = between<int>(9, -1);                   // empty second pack: rejected
int h4 = between<int, int>(9, 8, 1, 2);         // two elements, then m, then one: accepted
int h5 = between<int, int>(9, 8, -1, 2);        // m negative: rejected
int h6 = between<int, int>(-9, -8, 1, -2, -3);  // elements negative only: accepted
int h7 = between<int, int>(9, 8, -1, 2, 3);     // m negative: rejected

// Overloads that differ only in the condition on y: the right one is selected.
template <class... Ts> int pick(Ts... xs, int y) __attribute__((enable_if(y > 0, "positive"))) { return 0; }
template <class... Ts> char pick(Ts... xs, int y) __attribute__((enable_if(y <= 0, "not positive"))) { return 0; }
static_assert(sizeof(pick<int, int>(-1, -2, 3)) == sizeof(int), "");   // y positive: int overload
static_assert(sizeof(pick<int, int>(1, 2, -3)) == sizeof(char), "");   // y not positive: char overload

// Member templates: non-static (with the implicit object), const and static.
struct S {
  template <class... Ts>
  int m(Ts... xs, int y) __attribute__((enable_if(y > 0, "m"))) { return y; }
  template <class... Ts>
  int cm(Ts... xs, int y) const __attribute__((enable_if(y > 0, "cm"))) { return y; }
  template <class... Ts>
  static int sm(Ts... xs, int y) __attribute__((enable_if(y > 0, "sm"))) { return y; }
};
S s;
int i0 = s.m<int, int>(1, -2, 3);               // non-static: accepted
int i1 = s.m<int, int>(1, 2, -3);               // non-static: rejected
int i2 = s.cm<int, int>(1, -2, 3);              // const: accepted
int i3 = s.cm<int, int>(1, 2, -3);              // const: rejected
int i4 = S::sm<int, int>(1, -2, 3);             // static: accepted
int i5 = S::sm<int, int>(1, 2, -3);             // static: rejected

// A member template of a class template (the class parameter is not a pack).
template <class T>
struct W {
  template <class... Ts>
  int m(Ts... xs, T y) __attribute__((enable_if(y > 0, "W::m"))) { return 0; }
};
W<int> w;
int j0 = w.m<int, int>(1, -2, 3);               // accepted
int j1 = w.m<int, int>(1, 2, -3);               // rejected

// A class parameter pack and a parameter after it, in members that are not templates.
template <class... Cs>
struct C {
  int k(Cs... cs, int y) __attribute__((enable_if(y > 0, "C::k"))) { return y; }
  static int sk(Cs... cs, int y) __attribute__((enable_if(y > 0, "C::sk"))) { return y; }
  template <class T> int mt(Cs... cs, T t) __attribute__((enable_if(t > 0, "C::mt"))) { return 0; }
};
C<int, int> c;
C<> ce;
int k0 = c.k(1, 2, 3);                          // accepted
int k1 = c.k(1, 2, -3);                         // y negative: rejected
int k2 = c.k(-1, -2, 3);                        // elements negative only: accepted
int k3 = C<int, int>::sk(1, 2, 3);              // static: accepted
int k4 = C<int, int>::sk(1, 2, -3);             // static, y negative: rejected
int k5 = C<int, int>::sk(-1, -2, 3);            // static, elements negative only: accepted
int k6 = c.mt(1, 2, 3);                         // member template of the instance: accepted
int k7 = c.mt(1, 2, -3);                        // member template: rejected
int k8 = c.mt(-1, -2, 3);                       // member template, elements negative only: accepted
// An empty class pack, so that the parameter after it is the first (and only)
// parameter of the call.
int m0 = ce.k(3);                               // empty class pack: accepted
int m1 = ce.k(-3);                              // empty class pack: rejected
int m2 = C<>::sk(3);                            // empty class pack, static: accepted
int m3 = C<>::sk(-3);                           // empty class pack, static: rejected
int m4 = ce.mt(3);                              // empty class pack, member template: accepted
int m5 = ce.mt(-3);                             // empty class pack, member template: rejected

// A member template of a class template with a class parameter pack, whose
// condition names the pack and a parameter after it.  The elements of the
// class pack cannot be told apart (a separate defect: each reference is to
// the first one), so only calls that this decides correctly are tested: the
// first element is positive, and what is decided by t is the parameter after
// the pack.
template <class... Cs>
struct P {
  template <class T>
  int k(Cs... cs, T t) __attribute__((enable_if(((cs > 0) && ...) && t > 0, "P::k"))) { return 1; }
  template <class T>
  int s(Cs... cs, T t) __attribute__((enable_if(sizeof...(cs) == 2 && t > 0, "P::s"))) { return 1; }
};
P<int, int> p;
int q0 = p.k(1, 2, 3);                          // accepted
int q1 = p.k(1, 2, -3);                         // t negative: rejected
int q2 = p.k(-1, 2, 3);                         // first element negative: rejected
int q3 = p.s(1, 2, 3);                          // accepted
int q4 = p.s(1, 2, -3);                         // t negative: rejected

// A class pack after a function pack in a member template: the elements of the
// class pack are read as the first of them, not as an element of the function
// pack.
template <class... Cs>
struct Q {
  template <class... Ts>
  int k(Ts... ts, Cs... cs, int t) __attribute__((enable_if(((cs > 0) && ...) && t > 0, "Q::k"))) { return t; }
};
Q<int, int> q;
int w0 = q.k<int, int>(-1, -2, 5, 6, 7);        // accepted
int w1 = q.k<int, int>(1, 2, -5, -6, 7);        // class pack negative: rejected
int w2 = q.k<int, int>(1, 2, 5, 6, -7);         // t negative: rejected
int w3 = q.k<>(5, 6, 7);                        // empty function pack: accepted
int w4 = q.k<>(-5, 6, 7);                       // class pack negative: rejected

// A reference parameter after the pack: the call is rejected (it used to stop
// the front end with a segmentation fault).  Calls that are accepted by Clang
// are rejected too, as for a function without a pack: not tested.
template <class... Ts>
int ref(Ts... xs, const int &y) __attribute__((enable_if(y > 0, "reference"))) { return y; }
int r0 = ref<int, int>(5, 6, -3);               // y negative: rejected

// The parameters named in a trailing return type or an exception
// specification of a function template with a pack, which do not involve an
// enable_if condition, are not renumbered.
template <class... Ts>
auto tr(Ts... xs, int y) -> decltype(y + 1) { return y; }
int t0 = tr<int, int>(1, 2, 3);                 // accepted
template <class... Ts>
int ne(Ts... xs, int y) noexcept(noexcept(y + 1)) { return y; }
int t1 = ne<int, int>(1, 2, 3);                 // accepted
template <class T>
auto tr1(T t, int y) -> decltype(t + y) { return y; }
int t2 = tr1(1, 2);                             // accepted
