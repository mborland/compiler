//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
struct T { int t; T(int); T(const T &); ~T(); operator bool() const; };
struct P { int a; int b; };
T mkt(int);
P cond_ret(bool c) { return P{ c ? T(1).t : 2, 3 }; }
P plain_ret(int x) { return P{ mkt(x).t, 1 }; }
int ok(int n) { for (int i = 0; T q = mkt(i); ) { if (i++ > n) break; } return 0; }
