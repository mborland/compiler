extern "C" int printf(const char *, ...);
/* The trace is a string of digits, so that a long one cannot overflow. */
char trace[512];
int trace_length;
void note(int x) { trace[trace_length++] = (char)('0' + x); trace[trace_length] = 0; }
struct M { int v; M() : v(1) { note(1); } M(int x) : v(x) { note(x); } ~M() { note(9); } operator bool() const { return v != 0; } };
struct H { M arr[2]; int tail; H() : tail(5) { note(7); } ~H() { note(8); } };
int arrays() { M a[3]; M b[2] = { M(4), M(5) }; M c[2] = { 6, 7 }; M d[2][2]; return a[0].v + b[1].v + c[0].v + d[1][1].v; }
int member_arrays() { H h; return h.tail + h.arr[1].v; }
int conds(int c) {
  if (M m = M(c)) note(2); else note(3);
  while (M w = M(c)) { c--; if (c < 0) break; }
  for (int i = 0; M q = M(i - 1); ) { if (i++ == 1) break; }
  switch (M s = M(c); s.v) { case 0: note(4); break; default: note(5); }
  if (int x = c + 1; x > 0) note(6);
  return c;
}
M make(int x) { return M(x); }
int mixed() { int r = 0; for (int i = 0; i < 3; i++) { M t = make(i + 3); r += t.v; if (i == 1) continue; r++; } return r; }
int main() {
  printf("%d ", arrays()); printf("%s\n", trace); trace_length = 0; trace[0] = 0;
  printf("%d ", member_arrays()); printf("%s\n", trace); trace_length = 0; trace[0] = 0;
  printf("%d ", conds(2)); printf("%s\n", trace); trace_length = 0; trace[0] = 0;
  printf("%d ", conds(0)); printf("%s\n", trace); trace_length = 0; trace[0] = 0;
  printf("%d ", mixed()); printf("%s\n", trace); trace_length = 0; trace[0] = 0;
  return 0;
}
