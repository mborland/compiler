extern "C" int printf(const char *, ...);
int trace;
void note(int x) { trace = trace * 10 + x; }
struct A { int v; A(int x) : v(x) { note(1); } A(const A &o) : v(o.v) { note(2); } ~A() { note(3); } };
struct W { A a; int z; W(int x) : a(x), z(x + 1) {} };       // implicit copy ctor copies a via A(const A&)
struct P { int x, y; };
struct Q { P p; A a; Q() : p(), a(4) {} };                   // implicit copy: p bitwise, a copy ctor
struct Del { int d; Del() : Del(7) { note(5); } Del(int x) : d(x) { note(6); } };
A nrvo(int x) { A a(x); a.v += 1; return a; }
A early(int x) { A a(x); if (x > 3) return a; a.v = 0; return a; }
int by_val(A a) { return a.v; }
int use_w() { W w(3); W w2 = w; return w2.a.v + w2.z; }
int use_q() { Q q; Q q2(q); return q2.a.v + q2.p.x; }
int use_del() { Del d; return d.d; }
int arr_of_ptr() { A a(1); A *p = &a; return p->v; }
int main() {
  { A a = nrvo(5); printf("%d ", a.v); } printf("%d\n", trace); trace = 0;
  { A a = early(5); printf("%d ", a.v); } printf("%d\n", trace); trace = 0;
  { A a = early(2); printf("%d ", a.v); } printf("%d\n", trace); trace = 0;
  printf("%d ", by_val(A(8))); printf("%d\n", trace); trace = 0;
  A keep(9); trace = 0;
  printf("%d ", by_val(keep)); printf("%d\n", trace); trace = 0;
  printf("%d ", use_w()); printf("%d\n", trace); trace = 0;
  printf("%d ", use_q()); printf("%d\n", trace); trace = 0;
  printf("%d ", use_del()); printf("%d\n", trace); trace = 0;
  printf("%d\n", arr_of_ptr());
  return 0;
}
