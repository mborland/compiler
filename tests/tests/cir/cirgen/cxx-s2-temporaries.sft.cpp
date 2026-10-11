//type:rp
//options:--c++17
//require:BACK_END_TYPE 3
// Temporaries: lifetime extension by a reference (and by a member of an
// extended temporary), temporaries in conditions of if/while/for, in return
// statements and arguments, discarded results.
extern "C" int printf(const char *, ...);
int log_[256];
int n_;
void lg(int v) { log_[n_++] = v; }
struct T {
  int id;
  T(int i) : id(i) { lg(i); }
  ~T() { lg(id + 100); }
  int get() const { return id; }
  bool ok() const { return id > 0; }
};
struct P { T a; int k; };
T make(int i) { return T(i); }
int use(const T &t) { return t.id; }
int bad;
void expect(const int *want, int count, int code) {
  bool ok = n_ == count;
  for (int i = 0; ok && i < count; ++i) ok = log_[i] == want[i];
  if (!ok) {
    bad = bad * 10 + code;
    printf("case %d: got", code);
    for (int i = 0; i < n_; ++i) printf(" %d", log_[i]);
    printf("\n");
  }
  n_ = 0;
}
void extended() { const T &r = T(1); lg(2); const T &q = make(3); lg(use(r) + use(q)); }
void member_ext() { const int &k = P{T(5), 6}.k; lg(k); }
int in_return() { int a = T(7).get(); int b = make(8).get(); return a + b; }
void conditions() {
  if (T(9).ok()) lg(1);
  int i = 0;
  while (T(10 + i).get() < 12) { lg(2); ++i; }
  for (int j = 0; T(20 + j).get() < 22; ++j) lg(3);
}
void discarded() { make(30); T(31); (void)make(32).get(); lg(1); }
int main() {
  extended();
  { const int w[] = {1, 2, 3, 4, 103, 101}; expect(w, 6, 1); }
  member_ext();
  { const int w[] = {5, 6, 105}; expect(w, 3, 2); }
  if (in_return() != 15) bad = bad * 10 + 3;
  { const int w[] = {7, 107, 8, 108}; expect(w, 4, 3); }
  conditions();
  { const int w[] = {9, 109, 1, 10, 110, 2, 11, 111, 2, 12, 112, 20, 120, 3, 21, 121, 3, 22, 122}; expect(w, 19, 4); }
  discarded();
  { const int w[] = {30, 130, 31, 131, 32, 132, 1}; expect(w, 7, 5); }
  printf("bad=%d\n", bad);
  return bad != 0;
}
