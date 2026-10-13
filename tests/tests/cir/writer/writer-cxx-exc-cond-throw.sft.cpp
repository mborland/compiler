//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// throw in a conditional region: the region needs a FULLEXPR around it (a
// THROW_BEGIN in a conditional region needs an enclosing FULLEXPR), opened by
// the writer when the full expression has none and the conditional operands
// may throw (spec 12.5); a throw in a static initializer and in a MEMINIT.
int g(int);
struct A { A(); ~A(); };
int c1(bool c) { return c ? 5 : throw 7; }
bool c2(bool b) { return b || (throw 2, false); }
bool c3(bool b) { return b && (throw 3, true); }
int c4(bool c, bool d) { return c ? (d ? throw 1 : 2) : throw 3; }
int c5(bool c) { return g(c ? 1 : throw 4); }
int c6(bool c) { A a; return c ? 1 : throw A(); }
int sinit = g(0) ? throw 9 : 5;
struct M { int m; M(bool c) : m(c ? throw 8 : 6) { } };
void c7(bool c) { static int ls = c ? throw 8 : 6; (void)ls; }
