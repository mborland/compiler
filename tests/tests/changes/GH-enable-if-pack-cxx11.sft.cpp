//type:fn
//options_all:--clang_v 180000
//options:--c++11:--c++14
//remark:[GH #?] A function parameter pack named in an enable_if condition, C++11 and C++14
//match_regex:line 40: error: no\ instance\ of\ function\ template\ "two"\ matches\ the\ argument\ list
//match_regex:line 41: error: no\ instance\ of\ function\ template\ "two"\ matches\ the\ argument\ list
//match_regex:line 43: error: no\ instance\ of\ function\ template\ "two"\ matches\ the\ argument\ list
//match_regex:line 45: error: no\ instance\ of\ function\ template\ "two"\ matches\ the\ argument\ list
//match_regex:line 49: error: no\ instance\ of\ function\ template\ "none"\ matches\ the\ argument\ list
//match_regex:line 55: error: no\ instance\ of\ function\ template\ "three_big"\ matches\ the\ argument\ list
//match_regex:line 64: error: no\ instance\ of\ function\ template\ "after_first"\ matches\ the\ argument\ list
//match_regex:line 68: error: no\ instance\ of\ function\ template\ "head_and_count"\ matches\ the\ argument\ list
//match_regex:line 69: error: no\ instance\ of\ function\ template\ "head_and_count"\ matches\ the\ argument\ list
//match_regex:line 70: error: no\ instance\ of\ function\ template\ "head_and_count"\ matches\ the\ argument\ list
//match_regex:line 83: error: no\ instance\ of\ overloaded\ function\ "count"\ matches\ the\ argument\ list
//match_regex:line 101: error: no\ instance\ of\ function\ template\ "S::n"\ matches\ the\ argument\ list
//match_regex:line 103: error: no\ instance\ of\ function\ template\ "S::cn"\ matches\ the\ argument\ list
//match_regex:line 105: error: no\ instance\ of\ function\ template\ "S::sn"\ matches\ the\ argument\ list
//match_regex:line 113: error: no\ instance\ of\ function\ template\ "W<T>::m\ \[with\ T=int\]"\ matches\ the\ argument\ list
//match_regex:line 114: error: no\ instance\ of\ function\ template\ "W<T>::m\ \[with\ T=int\]"\ matches\ the\ argument\ list
//match_regex:line 115: error: no\ instance\ of\ function\ template\ "W<T>::m\ \[with\ T=int\]"\ matches\ the\ argument\ list
//match_regex:\A(?![\s\S]*line (?!(?:40|41|43|45|49|55|64|68|69|70|83|101|103|105|113|114|115)\b)\d+: error)
// 10/15/26  [GH #?]
//
// A function parameter pack named in an enable_if condition on a function
// template, in C++11 and C++14 (the conditions use sizeof...(pack) and an
// expansion of the pack in a call, not fold expressions).
//
// See GH-enable-if-pack for the explanation.  The pack was substituted as an
// empty pack in an instance, so sizeof...(args) was 0 and an expansion in a
// call had no arguments.
//
// This is now fixed.  Each call below is on its own line and is marked
// "accepted" or "rejected", which is what Clang does with it.  The expected
// output has one error, on that line, for each call marked "rejected" (a
// match_regex each), and none on any other line (the last match_regex).
// sizeof...(args): the number of arguments, by deduction and by explicit template arguments.
template <class... Ts>
int two(Ts... args) __attribute__((enable_if(sizeof...(args) == 2, "two"))) { return 2; }
int a0 = two();                                 // no arguments: rejected
int a1 = two(1);                                // one argument: rejected
int a2 = two(1, 2);                             // two arguments: accepted
int a3 = two(1, 2, 3);                          // three arguments: rejected
int a4 = two<int, int>(1, 2);                   // explicit template arguments, two: accepted
int a5 = two<int>(1);                           // explicit template arguments, one: rejected
template <class... Ts>
int none(Ts... args) __attribute__((enable_if(sizeof...(args) == 0, "none"))) { return 0; }
int a6 = none();                                // empty pack: accepted
int a7 = none(1);                               // one element: rejected

// A pack expansion in the arguments of a constexpr function call.
constexpr int sum3(int a, int b, int c) { return a + b + c; }
template <class... Ts>
int three_big(Ts... args) __attribute__((enable_if(sum3(args...) > 10, "sum3 > 10"))) { return 1; }
int e0 = three_big(1, 2, 3);                    // sum 6: rejected
int e1 = three_big(5, 5, 5);                    // sum 15: accepted
int e2 = three_big(11, 0, 0);                   // sum 11: accepted
int e3 = three_big(0, 0, 11);                   // sum 11: accepted

// A leading parameter and a pack: the pack is the rest.
template <class T, class... Ts>
int after_first(T first, Ts... rest) __attribute__((enable_if(sum3(rest...) == first, "rest sums to first"))) { return 1; }
int f0 = after_first(6, 1, 2, 3);               // 1 + 2 + 3 == 6: accepted
int f1 = after_first(7, 1, 2, 3);               // 6 != 7: rejected
template <class T, class... Ts>
int head_and_count(T head, Ts... rest) __attribute__((enable_if(head > 0 && sizeof...(rest) == 1, "head > 0, one rest"))) { return 1; }
int g0 = head_and_count(1, 2);                  // accepted
int g1 = head_and_count(-1, 2);                 // head negative: rejected
int g2 = head_and_count(1);                     // no rest: rejected
int g3 = head_and_count(1, 2, 3);               // two rest: rejected

// Overloads that differ only in the condition on sizeof...(args).
template <class... Ts>
char count(Ts... args) __attribute__((enable_if(sizeof...(args) == 1, "one"))) { return 1; }
template <class... Ts>
short count(Ts... args) __attribute__((enable_if(sizeof...(args) == 2, "two"))) { return 2; }
template <class... Ts>
int count(Ts... args) __attribute__((enable_if(sizeof...(args) > 2, "many"))) { return 3; }
static_assert(sizeof(count(1)) == sizeof(char), "");                 // one: char overload
static_assert(sizeof(count(1, 2)) == sizeof(short), "");             // two: short overload
static_assert(sizeof(count(1, 2, 3)) == sizeof(int), "");            // three: int overload
static_assert(sizeof(count(1, 2, 3, 4)) == sizeof(int), "");         // four: int overload
int j0 = count();                               // none: no overload: rejected
// An overload with enable_if is preferred over one without it, when enabled.
template <class... Ts> char pref(Ts... args) __attribute__((enable_if(sizeof...(args) == 2, "two"))) { return 1; }
template <class... Ts> int pref(Ts... args) { return 2; }
static_assert(sizeof(pref(1, 2)) == sizeof(char), "");               // enabled: char overload
static_assert(sizeof(pref(1)) == sizeof(int), "");                   // disabled: int overload

// Member templates: non-static, const and static, and a member template of a class template.
struct S {
  template <class... Ts>
  int n(Ts... args) __attribute__((enable_if(sizeof...(args) == 2, "n"))) { return 1; }
  template <class... Ts>
  int cn(Ts... args) const __attribute__((enable_if(sizeof...(args) == 2, "cn"))) { return 1; }
  template <class... Ts>
  static int sn(Ts... args) __attribute__((enable_if(sizeof...(args) == 2, "sn"))) { return 1; }
};
S s;
int k0 = s.n(1, 2);                             // non-static: accepted
int k1 = s.n(1);                                // non-static: rejected
int k2 = s.cn(1, 2);                            // const: accepted
int k3 = s.cn(1, 2, 3);                         // const: rejected
int k4 = S::sn(1, 2);                           // static: accepted
int k5 = S::sn();                               // static: rejected
template <class T>
struct W {
  template <class... Ts>
  int m(T t, Ts... xs) __attribute__((enable_if(t > 0 && sizeof...(xs) == 1, "W::m"))) { return 1; }
};
W<int> w;
int l0 = w.m(1, 2);                             // accepted
int l1 = w.m(-1, 2);                            // t negative: rejected
int l2 = w.m(1);                                // empty pack: rejected
int l3 = w.m(1, 2, 3);                          // two in the pack: rejected
