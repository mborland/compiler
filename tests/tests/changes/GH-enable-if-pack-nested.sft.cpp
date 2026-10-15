//type:fn
//options_all:--clang_v 180000
//options:--c++17:--c++20
//remark:[GH #?] Function types with their own function parameter pack nested in an enable_if condition
//match_regex:line 34: error: no\ instance\ of\ function\ template\ "g2"\ matches\ the\ argument\ list
//match_regex:line 37: error: no\ instance\ of\ function\ template\ "g3"\ matches\ the\ argument\ list
//match_regex:line 46: error: no\ instance\ of\ function\ template\ "k3"\ matches\ the\ argument\ list
//match_regex:line 49: error: no\ instance\ of\ function\ template\ "k4"\ matches\ the\ argument\ list
//match_regex:\A(?![\s\S]*line (?!(?:34|37|46|49)\b)\d+: error)
// 10/15/26  [GH #?]
//
// Function types with their own function parameter pack, nested in the
// enable_if condition of a function template (see GH-enable-if-pack), with and
// without a function parameter pack of the template itself: the pack of the
// nested type, expanded in its trailing return type or its exception
// specification, is another pack than the function's.
//
// The references to the packs of the function are given the elements of the
// instance's parameters; the nested type has packs of its own, whose
// elements are not those, and whose expansions must still work.  (Folds over
// the pack of the nested type are valid in C++17 and later; the names of the
// parameters of a function type in a function type in an attribute are not
// found in C++23, so it is not tested.)
//
// Each call below is on its own line and is marked "accepted" or "rejected",
// which is what Clang does with it.  The expected output has one error, on
// that line, for each call marked "rejected" (a match_regex each), and none on
// any other line (the last match_regex).
template <class... Ts> int g1(Ts... xs) __attribute__((enable_if(sizeof(auto (*)(Ts... ys) -> decltype((ys + ...))) > 0, "nested only"))) { return 0; }
int g1_0 = g1(1, 2);          // accepted
int g1_1 = g1(1);             // accepted
template <class... Ts> int g2(Ts... xs) __attribute__((enable_if(sizeof(auto (*)(Ts... ys) -> decltype((ys + ...))) > 0 && sizeof...(xs) == 2, "nested and sizeof"))) { return 0; }
int g2_0 = g2(1, 2);          // accepted
int g2_1 = g2(1);             // rejected
template <class... Ts> int g3(Ts... xs) __attribute__((enable_if(sizeof(auto (*)(Ts... ys) -> decltype(sizeof...(ys))) > 0 && ((xs > 0) && ...), "nested sizeof..."))) { return 0; }
int g3_0 = g3(1, 2);          // accepted
int g3_1 = g3(1, -2);         // rejected
// The same nested type in an ordinary trailing return type (no enable_if).
template <class... Ts> auto h(Ts... xs) -> decltype(sizeof(auto (*)(Ts... ys) -> decltype((ys + ...)))) { return 0; }
auto h0 = h(1, 2);            // accepted

// A nested function type with its own pack, in conditions of templates that have
// no function parameter pack.
template <class... Ts> int k3(int t) __attribute__((enable_if(sizeof(auto (*)(Ts... ys) -> decltype((ys + ...))) > 0 && t > 0, "class-free, no function pack"))) { return t; }
int k3_0 = k3<int, int>(1);     // accepted
int k3_1 = k3<int, int>(-1);    // rejected
template <class T> int k4(T t) __attribute__((enable_if(sizeof(auto (*)(T a, T b) -> decltype(a + b)) > 0 && t > 0, "no pack at all"))) { return t; }
int k4_0 = k4(1);               // accepted
int k4_1 = k4(-1);              // rejected
template <class... Ts> int k5(int t) __attribute__((enable_if(sizeof(auto (*)(Ts... ys) noexcept(noexcept((ys + ...))) -> int) > 0 && t > 0, "noexcept"))) { return t; }
int k5_0 = k5<int, int>(1);     // accepted
