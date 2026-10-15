//type:fp
//options:--c23:--c23 --clang_version=210000:--c23 --gnu_version=150000
//remark:[GH #?] A registered attribute can name the result of a function in C
//require:INCLUDE_EDG_TEST_ATTRIBUTES 1
// 10/14/26  [GH #?]
//
// The result of a function can be named in C, and the name denotes the result
// (a pointer if the function returns one).

int f1(int x) [[edg::r_post(r, r > x)]];
int f1(int y) [[edg::r_post(q, q > y)]] { return y + 1; }
int *f2(int *p) [[edg::r_post(r, r == p)]];
int f3(int x) [[edg::r_post(r, r > x)]] [[edg::r_pre(x > 0)]];
