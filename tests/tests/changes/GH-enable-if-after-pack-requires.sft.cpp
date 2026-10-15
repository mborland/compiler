//type:fn
//options_all:--clang_v 180000
//options:--c++20:--c++23
//remark:[GH #?] Requires-expressions, concepts and lambdas in an enable_if condition that names a parameter
//match_regex:line 42: error: no\ instance\ of\ function\ template\ "np1"\ matches\ the\ argument\ list
//match_regex:line 46: error: no\ instance\ of\ function\ template\ "np2"\ matches\ the\ argument\ list
//match_regex:line 52: error: no\ instance\ of\ function\ template\ "pl1"\ matches\ the\ argument\ list
//match_regex:line 56: error: no\ instance\ of\ function\ template\ "pl2"\ matches\ the\ argument\ list
//match_regex:line 62: error: no\ instance\ of\ function\ template\ "pf1"\ matches\ the\ argument\ list
//match_regex:line 66: error: no\ instance\ of\ function\ template\ "pf2"\ matches\ the\ argument\ list
//match_regex:line 68: error: no\ instance\ of\ function\ template\ "pf2"\ matches\ the\ argument\ list
//match_regex:line 72: error: no\ instance\ of\ function\ template\ "pf3"\ matches\ the\ argument\ list
//match_regex:line 73: error: no\ instance\ of\ function\ template\ "pf3"\ matches\ the\ argument\ list
//match_regex:line 83: error: no\ instance\ of\ function\ template\ "lm1"\ matches\ the\ argument\ list
//match_regex:line 87: error: no\ instance\ of\ function\ template\ "lm2"\ matches\ the\ argument\ list
//match_regex:line 95: error: no\ instance\ of\ function\ template\ "lm3"\ matches\ the\ argument\ list
//match_regex:line 100: error: no\ instance\ of\ function\ template\ "lm4"\ matches\ the\ argument\ list
//match_regex:\A(?![\s\S]*line (?!(?:42|46|52|56|62|66|68|72|73|83|87|95|100)\b)\d+: error)
// 10/15/26  [GH #?]
//
// Requires-expressions, concepts and lambdas in the enable_if condition of a
// function template that names a parameter, with and without a function
// parameter pack (see GH-enable-if-after-pack).
//
// The parameters of a requires-expression (and of a lambda) in the condition
// are other parameter lists, with their own numbers; they must not be taken
// for parameters of the function when the references to its parameters are
// renumbered for an instance with a pack.  If they were, the calls would be
// rejected, and the satisfaction of a concept that was evaluated at that point
// would be wrong for the rest of the translation unit (see the last lines).
//
// Each call below is on its own line and is marked "accepted" or "rejected",
// which is what Clang does with it.  The expected output has one error, on
// that line, for each call marked "rejected" (a match_regex each), and none on
// any other line (the last match_regex).
template <class T> concept Addable = requires (T a, T b) { a + b; };

// No pack.
template <class T>
int np1(T t) __attribute__((enable_if(Addable<T> && t > 0, "concept"))) { return t; }
int a0 = np1(1);                                // accepted
int a1 = np1(-1);                               // rejected
template <class T>
int np2(T t) __attribute__((enable_if(requires (T a, T b) { a + b; } && t > 0, "requires"))) { return t; }
int a2 = np2(1);                                // accepted
int a3 = np2(-1);                               // rejected

// A pack after the parameter (pack last).
template <class T, class... Ts>
int pl1(T t, Ts... xs) __attribute__((enable_if(Addable<T> && t > 0, "concept, pack last"))) { return t; }
int b0 = pl1<int, int>(1, 2);                   // accepted
int b1 = pl1<int, int>(-1, 2);                  // rejected
template <class... Ts>
int pl2(int x, Ts... xs) __attribute__((enable_if(requires (int v, int w) { v + w; } && x > 0, "requires, pack last"))) { return x; }
int b2 = pl2<int>(1, 2);                        // accepted
int b3 = pl2<int>(-1, 2);                       // rejected

// A pack first, and a parameter after it.
template <class... Ts>
int pf1(Ts... xs, int y) __attribute__((enable_if(requires { y + 1; } && y > 0, "requires without parameters"))) { return y; }
int c0 = pf1<int, int>(1, 2, 3);                // accepted
int c1 = pf1<int, int>(1, 2, -3);               // rejected
template <class... Ts>
int pf2(Ts... xs, int y) __attribute__((enable_if(requires (int v, int w) { v + w; } && y > 0, "requires with parameters"))) { return y; }
int c2 = pf2<int, int>(1, 2, 3);                // accepted
int c3 = pf2<int, int>(1, 2, -3);               // rejected
int c4 = pf2<>(3);                              // accepted
int c5 = pf2<>(-3);                             // rejected
template <class T, class... Ts>
int pf3(T t, Ts... xs, int y) __attribute__((enable_if(Addable<T> && t > 0 && y > 0, "concept, parameters around the pack"))) { return y; }
int c6 = pf3<int, int, int>(1, 2, 3, 4);        // accepted
int c7 = pf3<int, int, int>(1, 2, 3, -4);       // rejected
int c8 = pf3<int, int, int>(-1, 2, 3, 4);       // rejected
template <class... Ts>
int pf4(Ts... xs, int y) __attribute__((enable_if(requires (int v) { v + y; } && y > 0, "requires naming y"))) { return y; }
int c9 = pf4<int, int>(1, 2, 3);                // accepted

// Lambdas, whose parameters (and their trailing return types) are other
// parameter lists.
template <class... Ts>
int lm1(Ts... xs, int y) __attribute__((enable_if([](int v) constexpr { return v > 0; }(y), "lambda"))) { return y; }
int d0 = lm1<int, int>(1, 2, 3);                // accepted
int d1 = lm1<int, int>(1, 2, -3);               // rejected
template <class... Ts>
int lm2(Ts... xs, int y) __attribute__((enable_if([](int v) constexpr -> decltype(v) { return v; }(y) > 0, "lambda, trailing return type"))) { return y; }
int d2 = lm2<int, int>(1, 2, 3);                // accepted
int d3 = lm2<int, int>(1, 2, -3);               // rejected

// Lambdas with more parameters than the function has, whose trailing return
// types name their last parameter: the numbers (3 and 2) are those of no
// parameter of the function.
template <class... Ts>
int lm3(Ts... xs, int y) __attribute__((enable_if([](int v, int w, int u) constexpr -> decltype(u) { return v; }(y, 0, 0) > 0, "lambda, third parameter"))) { return y; }
int d4 = lm3<int, int>(1, 2, 3);                // accepted
int d5 = lm3<int, int>(1, 2, -3);               // rejected
int d6 = lm3<>(3);                              // accepted
template <class T>
int lm4(T t) __attribute__((enable_if([](int v, int w) constexpr -> decltype(w) { return v; }(t, 0) > 0, "lambda, second parameter, no pack"))) { return t; }
int d7 = lm4(3);                                // accepted
int d8 = lm4(-3);                               // rejected

// The concept is still satisfied by int afterwards and is still usable
// elsewhere: nothing in the conditions above has changed its cached result.
static_assert(Addable<int>, "Addable<int> must hold");
template <class T> requires Addable<T> int plain(T t) { return t; }
int e0 = plain(1);                              // accepted
