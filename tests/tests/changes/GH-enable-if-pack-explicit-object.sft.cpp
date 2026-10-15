//type:fn
//options_all:--clang_v 180000
//options:--c++23
//remark:[GH #?] enable_if conditions naming parameters and packs, with an explicit object parameter
//match_regex:line 33: error: no\ instance\ of\ function\ template\ "S::a"\ matches\ the\ argument\ list
//match_regex:line 35: error: no\ instance\ of\ function\ template\ "S::b"\ matches\ the\ argument\ list
//match_regex:line 36: error: no\ instance\ of\ function\ template\ "S::b"\ matches\ the\ argument\ list
//match_regex:line 38: error: no\ instance\ of\ function\ template\ "S::c"\ matches\ the\ argument\ list
//match_regex:line 39: error: no\ instance\ of\ function\ template\ "S::c"\ matches\ the\ argument\ list
//match_regex:line 41: error: no\ instance\ of\ function\ template\ "S::d"\ matches\ the\ argument\ list
//match_regex:line 42: error: no\ instance\ of\ function\ template\ "S::d"\ matches\ the\ argument\ list
//match_regex:\A(?![\s\S]*line (?!(?:33|35|36|38|39|41|42)\b)\d+: error)
// 10/15/26  [GH #?]
//
// Parameters and function parameter packs named in an enable_if condition of
// a member template with a C++23 explicit object parameter.  (See
// GH-enable-if-pack and GH-enable-if-after-pack.)  Clang 21 rejects every
// call to such a function, so the expected result is that of enable_if
// semantics: the calls marked "rejected" are the ones whose condition is false.
// With an explicit object parameter the first explicit template argument binds to Self.
struct S {
  template <class Self>
  int a(this Self&& self, int x) __attribute__((enable_if(x > 0, "x positive"))) { return x; }
  template <class Self, class... Ts>
  int b(this Self&& self, Ts... xs) __attribute__((enable_if(((xs > 0) && ...), "all positive"))) { return 1; }
  template <class Self, class... Ts>
  int c(this Self&& self, Ts... xs, int y) __attribute__((enable_if(y > 0 && sizeof...(xs) == 2, "y > 0, two"))) { return y; }
  template <class Self, class... Ts>
  int d(this Self&& self, int x, Ts... xs) __attribute__((enable_if(x > 0 && sizeof...(xs) == 1, "x > 0, one"))) { return x; }
};
S s;
int a0 = s.a(1);                                // accepted
int a1 = s.a(-1);                               // rejected
int b0 = s.b(1, 2);                             // accepted
int b1 = s.b(1, -2);                            // rejected
int b2 = s.b(-1, 2);                            // rejected
int c0 = s.c<S&, int, int>(1, 2, 3);            // accepted
int c1 = s.c<S&, int, int>(1, 2, -3);           // rejected
int c2 = s.c<S&, int>(1, 3);                    // rejected
int d0 = s.d(1, 2);                             // accepted
int d1 = s.d(-1, 2);                            // rejected
int d2 = s.d(1, 2, 3);                          // rejected
