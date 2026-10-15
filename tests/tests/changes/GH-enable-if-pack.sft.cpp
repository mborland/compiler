//type:fn
//options_all:--clang_v 180000
//options:--c++17:--c++20:--c++23
//remark:[GH #?] A function parameter pack named in an enable_if condition
//match_regex:line 66: error: no\ instance\ of\ function\ template\ "two"\ matches\ the\ argument\ list
//match_regex:line 67: error: no\ instance\ of\ function\ template\ "two"\ matches\ the\ argument\ list
//match_regex:line 69: error: no\ instance\ of\ function\ template\ "two"\ matches\ the\ argument\ list
//match_regex:line 71: error: no\ instance\ of\ function\ template\ "two"\ matches\ the\ argument\ list
//match_regex:line 75: error: no\ instance\ of\ function\ template\ "none"\ matches\ the\ argument\ list
//match_regex:line 82: error: no\ instance\ of\ function\ template\ "positive"\ matches\ the\ argument\ list
//match_regex:line 84: error: no\ instance\ of\ function\ template\ "positive"\ matches\ the\ argument\ list
//match_regex:line 85: error: no\ instance\ of\ function\ template\ "positive"\ matches\ the\ argument\ list
//match_regex:line 87: error: no\ instance\ of\ function\ template\ "positive"\ matches\ the\ argument\ list
//match_regex:line 88: error: no\ instance\ of\ function\ template\ "positive"\ matches\ the\ argument\ list
//match_regex:line 89: error: no\ instance\ of\ function\ template\ "positive"\ matches\ the\ argument\ list
//match_regex:line 94: error: no\ instance\ of\ function\ template\ "anyneg"\ matches\ the\ argument\ list
//match_regex:line 95: error: no\ instance\ of\ function\ template\ "anyneg"\ matches\ the\ argument\ list
//match_regex:line 102: error: no\ instance\ of\ function\ template\ "bigsum"\ matches\ the\ argument\ list
//match_regex:line 103: error: no\ instance\ of\ function\ template\ "bigsum"\ matches\ the\ argument\ list
//match_regex:line 109: error: no\ instance\ of\ function\ template\ "bigsum1"\ matches\ the\ argument\ list
//match_regex:line 115: error: no\ instance\ of\ function\ template\ "sum6"\ matches\ the\ argument\ list
//match_regex:line 121: error: no\ instance\ of\ function\ template\ "three_big"\ matches\ the\ argument\ list
//match_regex:line 130: error: no\ instance\ of\ function\ template\ "after_first"\ matches\ the\ argument\ list
//match_regex:line 135: error: no\ instance\ of\ function\ template\ "head_gt_rest"\ matches\ the\ argument\ list
//match_regex:line 137: error: no\ instance\ of\ function\ template\ "head_gt_rest"\ matches\ the\ argument\ list
//match_regex:line 138: error: no\ instance\ of\ function\ template\ "head_gt_rest"\ matches\ the\ argument\ list
//match_regex:line 142: error: no\ instance\ of\ function\ template\ "head_and_count"\ matches\ the\ argument\ list
//match_regex:line 143: error: no\ instance\ of\ function\ template\ "head_and_count"\ matches\ the\ argument\ list
//match_regex:line 144: error: no\ instance\ of\ function\ template\ "head_and_count"\ matches\ the\ argument\ list
//match_regex:line 152: error: no\ instance\ of\ function\ template\ "packs"\ matches\ the\ argument\ list
//match_regex:line 154: error: no\ instance\ of\ function\ template\ "packs"\ matches\ the\ argument\ list
//match_regex:line 159: error: no\ instance\ of\ function\ template\ "pos"\ matches\ the\ argument\ list
//match_regex:line 160: error: no\ instance\ of\ function\ template\ "pos"\ matches\ the\ argument\ list
//match_regex:line 162: error: no\ instance\ of\ function\ template\ "pos"\ matches\ the\ argument\ list
//match_regex:line 164: error: no\ instance\ of\ function\ template\ "pos"\ matches\ the\ argument\ list
//match_regex:line 168: error: no\ instance\ of\ function\ template\ "pos"\ matches\ the\ argument\ list
//match_regex:line 169: error: no\ instance\ of\ function\ template\ "pos"\ matches\ the\ argument\ list
//match_regex:line 185: error: no\ instance\ of\ overloaded\ function\ "count"\ matches\ the\ argument\ list
//match_regex:line 203: error: no\ instance\ of\ function\ template\ "S::n"\ matches\ the\ argument\ list
//match_regex:line 205: error: no\ instance\ of\ function\ template\ "S::cn"\ matches\ the\ argument\ list
//match_regex:line 207: error: no\ instance\ of\ function\ template\ "S::sn"\ matches\ the\ argument\ list
//match_regex:line 215: error: no\ instance\ of\ function\ template\ "W<T>::m\ \[with\ T=int\]"\ matches\ the\ argument\ list
//match_regex:line 216: error: no\ instance\ of\ function\ template\ "W<T>::m\ \[with\ T=int\]"\ matches\ the\ argument\ list
//match_regex:line 217: error: no\ instance\ of\ function\ template\ "W<T>::m\ \[with\ T=int\]"\ matches\ the\ argument\ list
//match_regex:line 222: error: no\ instance\ of\ function\ template\ "lambda\ \[\]\(auto\.\.\.\)\->auto"\ matches\ the\ argument\ list
//match_regex:\A(?![\s\S]*line (?!(?:66|67|69|71|75|82|84|85|87|88|89|94|95|102|103|109|115|121|130|135|137|138|142|143|144|152|154|159|160|162|164|168|169|185|203|205|207|215|216|217|222)\b)\d+: error)
// 10/15/26  [GH #?]
//
// A function parameter pack named in an enable_if condition on a function
// template is substituted as an empty pack in an instance.
//
// For an instance, the condition was substituted with no elements for the
// function parameter pack, so sizeof...(pack) was 0 and a fold over the pack
// had the value of an empty fold, whatever the arguments of the call.  A call
// was enabled or disabled as if it had no arguments (and the fold
// (args + ...) > 5 gave a spurious "empty expansion not valid for this fold
// expression" error).
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

// A fold over && : all arguments positive (empty fold is true).
template <class... Ts>
int positive(Ts... args) __attribute__((enable_if(((args > 0) && ...), "all positive"))) { return 1; }
int b0 = positive();                            // empty pack: accepted
int b1 = positive(1);                           // one element: accepted
int b2 = positive(-1);                          // one element: rejected
int b3 = positive(1, 2);                        // two elements: accepted
int b4 = positive(1, -2);                       // second negative: rejected
int b5 = positive(-1, 2);                       // first negative: rejected
int b6 = positive(1, 2, 3);                     // three elements: accepted
int b7 = positive(1, 2, -3);                    // third negative: rejected
int b8 = positive(1, -2, 3);                    // middle negative: rejected
int b9 = positive(-1, 2, 3);                    // first negative: rejected

// A fold over || : some argument negative (empty fold is false).
template <class... Ts>
int anyneg(Ts... args) __attribute__((enable_if(((args < 0) || ...), "some negative"))) { return 1; }
int c0 = anyneg();                              // empty pack: rejected
int c1 = anyneg(1, 2);                          // none negative: rejected
int c2 = anyneg(1, -2);                         // last negative: accepted
int c3 = anyneg(-1, 2);                         // first negative: accepted

// Folds over + compared with a constant: binary fold with an initial value, and unary folds.
template <class... Ts>
int bigsum(Ts... args) __attribute__((enable_if((args + ... + 0) > 5, "sum > 5"))) { return 1; }
int d0 = bigsum();                              // empty pack, sum 0: rejected
int d1 = bigsum(1, 2);                          // sum 3: rejected
int d2 = bigsum(3, 3);                          // sum 6: accepted
int d3 = bigsum(5, 1);                          // sum 6: accepted
int d4 = bigsum(1, 1, 4);                       // sum 6: accepted
template <class... Ts>
int bigsum1(Ts... args) __attribute__((enable_if((args + ...) > 5, "sum > 5"))) { return 1; }
int d5 = bigsum1(1, 2);                         // unary fold, sum 3: rejected
int d6 = bigsum1(3, 3);                         // unary fold, sum 6: accepted
int d7 = bigsum1(1, 1, 4);                      // unary fold, sum 6: accepted
template <class... Ts>
int sum6(Ts... args) __attribute__((enable_if((0 + ... + args) == 6, "sum == 6"))) { return 1; }
int d8 = sum6(1, 2, 3);                         // left fold, sum 6: accepted
int d9 = sum6(1, 2, 4);                         // left fold, sum 7: rejected

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
int head_gt_rest(T head, Ts... rest) __attribute__((enable_if(((head > rest) && ...), "head > all rest"))) { return 1; }
int g0 = head_gt_rest(5);                       // empty rest: accepted
int g1 = head_gt_rest(5, 1);                    // one element, smaller: accepted
int g2 = head_gt_rest(5, 9);                    // one element, larger: rejected
int g3 = head_gt_rest(5, 1, 2);                 // two elements, both smaller: accepted
int g4 = head_gt_rest(5, 1, 9);                 // last larger: rejected
int g5 = head_gt_rest(5, 9, 1);                 // first larger: rejected
template <class T, class... Ts>
int head_and_count(T head, Ts... rest) __attribute__((enable_if(head > 0 && sizeof...(rest) == 1, "head > 0, one rest"))) { return 1; }
int g6 = head_and_count(1, 2);                  // accepted
int g7 = head_and_count(-1, 2);                 // head negative: rejected
int g8 = head_and_count(1);                     // no rest: rejected
int g9 = head_and_count(1, 2, 3);               // two rest: rejected

// Two function parameter packs (the first with explicit template arguments), one named in a fold.
template <class... Ts, class... Us>
int packs(Ts... as, Us... bs) __attribute__((enable_if(sizeof...(as) == 2 && ((bs > 0) && ...), "two as, all bs positive"))) { return 1; }
int h0 = packs<int, int>(9, 9);                 // empty second pack: accepted
int h1 = packs<int, int>(9, 9, 1);              // accepted
int h2 = packs<int, int>(9, 9, 1, 2);           // accepted
int h3 = packs<int, int>(9, 9, 1, -2);          // second pack has a negative: rejected
int h4 = packs<int, int>(-9, -9, 1, 2);         // first pack negative only: accepted
int h5 = packs<int>(9, 1);                      // one in the first pack: rejected
// Arguments that are converted: double, char and unsigned elements.
template <class... Ts>
int pos(Ts... args) __attribute__((enable_if(((args > 0) && ...), "all positive"))) { return 1; }
int i0 = pos(1.5, 2);                           // double and int: accepted
int i1 = pos(0.5, -2);                          // second negative: rejected
int i2 = pos(-0.5, 2);                          // first negative: rejected
int i3 = pos('a', 1);                           // char: accepted
int i4 = pos('a', -1);                          // char then negative: rejected
int i5 = pos(1u, 2u);                           // unsigned: accepted
int i6 = pos(0u, 1u);                           // unsigned zero: rejected

// An argument that is not a constant expression disables the candidate.
int nonconst(int v) {
  int r0 = pos(v, 1);                           // first not constant: rejected
  int r1 = pos(1, v);                           // second not constant: rejected
  int r2 = pos(1, 2);                           // constants: accepted
  return r0 + r1 + r2;
}

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
  int n(Ts... args) __attribute__((enable_if(((args > 0) && ...), "n"))) { return 1; }
  template <class... Ts>
  int cn(Ts... args) const __attribute__((enable_if(((args > 0) && ...), "cn"))) { return 1; }
  template <class... Ts>
  static int sn(Ts... args) __attribute__((enable_if(((args > 0) && ...), "sn"))) { return 1; }
};
S s;
int k0 = s.n(1, 2);                             // non-static: accepted
int k1 = s.n(1, -2);                            // non-static: rejected
int k2 = s.cn(1, 2);                            // const: accepted
int k3 = s.cn(-1, 2);                           // const: rejected
int k4 = S::sn(1, 2);                           // static: accepted
int k5 = S::sn(1, -2);                          // static: rejected
template <class T>
struct W {
  template <class... Ts>
  int m(T t, Ts... xs) __attribute__((enable_if(t > 0 && sizeof...(xs) == 1 && ((xs > 0) && ...), "W::m"))) { return 1; }
};
W<int> w;
int l0 = w.m(1, 2);                             // accepted
int l1 = w.m(-1, 2);                            // t negative: rejected
int l2 = w.m(1, -2);                            // xs negative: rejected
int l3 = w.m(1, 2, 3);                          // two in the pack: rejected

// A generic lambda.
auto lam = [](auto... a) __attribute__((enable_if(sizeof...(a) == 2, "two"))) { return 1; };
int m0 = lam(1, 2);                             // two arguments: accepted
int m1 = lam(1);                                // one argument: rejected
