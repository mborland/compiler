//type:rp
//options:--c++17
//require:BACK_END_TYPE 3
// The comma operator discarding class prvalues: a temporary built by a
// constructor, and a call result (the left operand of a comma is a discarded
// value, which for a class with a destructor is materialized and destroyed).
extern "C" int printf(const char *, ...);
int log_[256];
int n_;
void lg(int v) { log_[n_++] = v; }
struct T {
  int id;
  T(int i) : id(i) { lg(i); }
  ~T() { lg(id + 100); }
};
T make(int i) { return T(i); }
int c1() { return (T(40), T(41), 5); }
int c2() { return (make(50), T(51), make(52), 6); }
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
int main() {
  if (c1() != 5) bad = bad * 10 + 1;
  { const int w[] = {40, 41, 141, 140}; expect(w, 4, 2); }
  if (c2() != 6) bad = bad * 10 + 3;
  { const int w[] = {50, 51, 52, 152, 151, 150}; expect(w, 6, 4); }
  printf("bad=%d\n", bad);
  return bad != 0;
}
