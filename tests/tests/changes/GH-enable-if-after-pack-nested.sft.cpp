//type:fn
//options_all:--clang_v 180000
//options:--c++11:--c++14:--c++17:--c++20
//remark:[GH #?] Nested function types in an enable_if condition that name their own parameters
//match_regex:line 36: error: no\ instance\ of\ function\ template\ "nfa"\ matches\ the\ argument\ list
//match_regex:line 40: error: no\ instance\ of\ function\ template\ "nfb"\ matches\ the\ argument\ list
//match_regex:line 44: error: no\ instance\ of\ function\ template\ "nfc"\ matches\ the\ argument\ list
//match_regex:line 48: error: no\ instance\ of\ function\ template\ "nfd"\ matches\ the\ argument\ list
//match_regex:line 53: error: no\ instance\ of\ function\ template\ "nfe"\ matches\ the\ argument\ list
//match_regex:line 57: error: no\ instance\ of\ function\ template\ "nff"\ matches\ the\ argument\ list
//match_regex:line 61: error: no\ instance\ of\ function\ template\ "nfg"\ matches\ the\ argument\ list
//match_regex:line 65: error: no\ instance\ of\ function\ template\ "nfh"\ matches\ the\ argument\ list
//match_regex:\A(?![\s\S]*line (?!(?:36|40|44|48|53|57|61|65)\b)\d+: error)
// 10/15/26  [GH #?]
//
// Nested function types in the enable_if condition of a function template that
// name the parameters of their own parameter lists (see GH-enable-if-after-
// pack).
//
// The references to those parameters, made from the trailing return type or
// the exception specification of the nested type, have the number of a
// parameter of that type, which no parameter of the function has (or one that
// it has, but another one): they must not be renumbered as if they referred to
// the parameters of the function, or valid calls are rejected.  The last
// template has a pack and a parameter after it.
//
// Each call below is on its own line and is marked "accepted" or "rejected",
// which is what Clang does with it.  The expected output has one error, on
// that line, for each call marked "rejected" (a match_regex each), and none on
// any other line (the last match_regex).  (Not tested in C++23: the names of
// the parameters of a function type in the trailing return type of a function
// type in an attribute are not found there.)
template <class T>
int nfa(T t) __attribute__((enable_if(sizeof(auto (*)(T pa, T pb, T pc) -> decltype(pc)) > 0 && t > 0, "trailing return"))) { return t; }
int u0 = nfa(3);                                 // accepted
int u1 = nfa(-3);                                // rejected
template <class T>
int nfb(T t) __attribute__((enable_if(sizeof(void (*)(T pa, T pb, T pc) noexcept(noexcept(pc + 1))) > 0 && t > 0, "noexcept"))) { return t; }
int u2 = nfb(3);                                 // accepted
int u3 = nfb(-3);                                // rejected
template <class T>
int nfc(T t) __attribute__((enable_if(sizeof(auto (*)(T pa, T pb, T pc) -> decltype(pc + 1)) > 0 && t > 0, "decltype(pc + 1)"))) { return t; }
int u4 = nfc(3);                                 // accepted
int u5 = nfc(-3);                                // rejected
template <class T>
int nfd(T t) __attribute__((enable_if(sizeof(decltype(static_cast<auto (*)(T pa, T pb, T pc) -> decltype(pc * pa)>(nullptr))) > 0 && t > 0, "cast"))) { return t; }
int u6 = nfd(3);                                 // accepted
int u7 = nfd(-3);                                // rejected
template <class T> struct X {};
template <class T>
int nfe(T t) __attribute__((enable_if(sizeof(X<auto (*)(T pa, T pb, T pc) -> decltype(pc)>) > 0 && t > 0, "template argument"))) { return t; }
int u8 = nfe(3);                                 // accepted
int u9 = nfe(-3);                                // rejected
template <class T>
int nff(T t) __attribute__((enable_if(sizeof(auto (*)(auto (*)(T pa, T pb, T pc) -> decltype(pc), T pd) -> decltype(pd)) > 0 && t > 0, "doubly nested"))) { return t; }
int v0 = nff(3);                                // accepted
int v1 = nff(-3);                               // rejected
template <class T, class U>
int nfg(T t, U u) __attribute__((enable_if(sizeof(auto (*)(T pa, T pb, T pc) -> decltype(pc)) > 0 && u > t, "own parameters after the nested type"))) { return t; }
int v2 = nfg(1, 3);                             // accepted
int v3 = nfg(3, 1);                             // rejected
template <class... Ts>
int nfh(Ts... xs, int y) __attribute__((enable_if(sizeof(auto (*)(Ts... pa, int pb, int pc, int pd) -> decltype(pd)) > 0 && y > 0, "after a pack"))) { return y; }
int v4 = nfh<int, int>(1, 2, 3);                 // accepted
int v5 = nfh<int, int>(1, 2, -3);                // rejected
