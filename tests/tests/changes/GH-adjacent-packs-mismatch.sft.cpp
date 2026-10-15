//type:fn
//options:--c++11:--c++17:--c++23
//remark:[GH #?] Two function parameter packs in a row of different lengths are still an error
//match_regex:line 15: error: no instance of function template "tr" matches the argument list
//match_regex:\A(?![\s\S]*line (?!15\b)\d+: error)
// 10/15/26  [GH #?]
//
// See GH-adjacent-packs.  Packs that do not have the same number of elements
// are still an error: the first call below has two elements in each pack, the
// second has two and one.  The error is on the second call and only there.
template <class... Vs> int g(Vs...);
template <class... Ts, class... Us>
auto tr(Ts... as, Us... bs) -> decltype(g((as + bs)...)) { return 0; }
int a0 = tr<int, int>(1, 2, 3, 4);
int a1 = tr<int, int>(1, 2, 3);
