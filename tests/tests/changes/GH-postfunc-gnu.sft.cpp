//type:fp
//options:--c++20 --gnu_version=150000
//remark:[GH #?] Contracts of a function with C language linkage declared in two namespaces, in GNU mode
// 10/14/26  [GH #?]
//
// A function with C language linkage that is declared in two namespaces is
// one function in the standard.  In GNU emulation the front end keeps a
// separate routine for each declaration, so declarations with different
// contracts are not compared: no mismatch is reported.  (This is a
// limitation of the GNU emulation; in the other modes there is one routine,
// and different contracts are an error: see GH-postfunc-err2.)

namespace N { extern "C" int f(int x) [[edg::r_pre(x > 0)]]; }
extern "C" int f(int x) [[edg::r_pre(x > 1)]];
