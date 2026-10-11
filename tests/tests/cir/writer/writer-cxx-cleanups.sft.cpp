//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
int trace;
struct T { int t; T(int x); T(const T &o); ~T(); int get() const { return t; } T inc() const { return T(t + 1); } };
T mk(int);
int locals(int c) { T a(c); if (c) return 1; T b(2); return a.t + b.t; }
int loops(int n) { int s = 0; for (int i = 0; i < n; i++) { T a(i); if (i == 2) break; if (i == 1) continue; s += a.t; } return s; }
int temps() { return mk(1).get() + mk(2).inc().inc().get(); }
int cond_temps(bool c) { return c ? T(1).get() : 0; }
int by_value(T t);
int args() { T x(1); return by_value(x) + by_value(mk(3)); }
int extended() { const T &r = mk(5); return r.get(); }
T nrvo(int x) { T a(x); a.t += 1; return a; }
T early(int x) { T a(x); if (x > 3) return a; a.t = 0; return a; }
T pick(bool c, const T &a, const T &b) { return c ? a : b; }
T cond_class(bool c) { T r = c ? mk(1) : mk(2); return r; }
void drop() { mk(7); }
