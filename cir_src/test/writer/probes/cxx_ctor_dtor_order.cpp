extern "C" int printf(const char *, ...);
int trace;
void note(int x) { trace = trace * 10 + x; }
struct B { int b; B(int x) : b(x) { note(1); } ~B() { note(2); } };
struct M { int m; M() : m(5) { note(3); } M(const M &o) : m(o.m) { note(8); } ~M() { note(4); } int get() const { return m; } };
struct D : B { M mm; int k = 7; D() : B(9) { note(5); } ~D() { note(6); } };
M mk() { return M(); }
int take(M x) { return x.get(); }
int tmp() { return mk().get() + take(mk()); }
int condtmp(bool c) { return c ? M().get() : 0; }
int ref() { const M &r = mk(); note(7); return r.get(); }
int blocks() { int s = 0; for (int i = 0; i < 2; i++) { M a; if (i) break; s += a.get(); } return s; }
int early(int c) { M a; if (c) return 1; M b; return 2; }
int main() {
  { D d; note(0); }
  printf("%d\n", trace); trace = 0;
  printf("%d ", tmp()); printf("%d\n", trace); trace = 0;
  printf("%d ", condtmp(true)); printf("%d ", condtmp(false)); printf("%d\n", trace); trace = 0;
  printf("%d ", ref()); printf("%d\n", trace); trace = 0;
  printf("%d ", blocks()); printf("%d\n", trace); trace = 0;
  printf("%d ", early(1)); printf("%d ", early(0)); printf("%d\n", trace);
  return 0;
}
