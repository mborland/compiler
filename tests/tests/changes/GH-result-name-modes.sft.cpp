//type:fp
//options:--c++20:--c++20 --clang_version=210000:--c++20 --gnu_version=150000
//remark:[GH #?] A registered attribute can name the result of a function in every mode
//require:INCLUDE_EDG_TEST_ATTRIBUTES 1
// 10/14/26  [GH #?]
//
// The result name works in the clang and GNU modes (where the parameters of
// a function are invisible until the function body in some of them), and
// with the other attributes after the parameter list.

int f1(int x) [[edg::r_post(r, r > x)]];
int f1(int y) [[edg::r_post(res, res > y)]] { return y + 1; }
int f2(int x) [[edg::r_post(r, r > x)]] [[edg::r_pre(x > 0)]];
struct S { int m(int x) [[edg::r_post(r, r > x && this != nullptr)]]; };
template <class T> T h(T x) [[edg::r_post(r, r > x)]];
template <class T> T h(T y) [[edg::r_post(z, z > y)]] { return y; }
int u() { return h(1); }
