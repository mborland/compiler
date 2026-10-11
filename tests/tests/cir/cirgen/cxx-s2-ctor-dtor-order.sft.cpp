//type:rp
//options:--c++17
//require:BACK_END_TYPE 3
// Constructor and destructor order (slice S2): scopes, loops with break and
// continue, return and goto through cleanups, full-expression temporaries,
// conditional temporaries (?: inside &&, &&), bases and members (initialization in
// declaration order, destruction in reverse), arrays, delegating constructors.
extern "C" int printf(const char *, ...);
int log_[256];
int n_;
void lg(int v) { log_[n_++] = v; }
struct T {
  int id;
  bool ok() const { return id > 0; }
  T(int i) : id(i) { lg(i); }
  T(const T &o) : id(o.id + 1000) { lg(id); }
  ~T() { lg(id + 100); }
};
struct Base { T b; Base(int i) : b(i) {} };
struct Member { T m1; T m2; Member() : m1(41), m2(42) {} };
struct Derived : Base { T d1; Member mem; T d2; Derived() : Base(31), d1(32), d2(33) {} };
struct Deleg { T a; Deleg(int x) : a(x) {} Deleg() : Deleg(77) { lg(1); } };

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

void scopes() { T a(1); { T b(2); T c(3); } T d(4); }
void loop() {
  for (int i = 0; i < 3; ++i) {
    T x(10 + i);
    if (i == 1) continue;
    if (i == 2) break;
    lg(50 + i);
  }
}
int ret(int v) { T a(20); if (v) { T b(21); return 7; } lg(60); return 8; }
void jump() { { T a(30); goto out; lg(99); } out: lg(61); }
bool temps(int a) { return T(41).ok() && (a ? T(42).ok() : true); }
bool cond(int a) { return a && T(7).id > 0; }
void members() { Derived d; lg(2); }
void array() { T arr[3] = {T(80), T(81), T(82)}; lg(90); }
void delegating() { Deleg d; lg(2); }
int main() {
  scopes();
  { const int w[] = {1, 2, 3, 103, 102, 4, 104, 101}; expect(w, 8, 1); }
  loop();
  { const int w[] = {10, 50, 110, 11, 111, 12, 112}; expect(w, 7, 2); }
  if (ret(1) != 7) bad = bad * 10 + 3;
  { const int w[] = {20, 21, 121, 120}; expect(w, 4, 3); }
  if (ret(0) != 8) bad = bad * 10 + 3;
  { const int w[] = {20, 60, 120}; expect(w, 3, 3); }
  jump();
  { const int w[] = {30, 130, 61}; expect(w, 3, 4); }
  if (!temps(1)) bad = bad * 10 + 5;
  { const int w[] = {41, 42, 142, 141}; expect(w, 4, 5); }
  if (!temps(0)) bad = bad * 10 + 5;
  { const int w[] = {41, 141}; expect(w, 2, 5); }
  if (!cond(1) || cond(0)) bad = bad * 10 + 6;
  { const int w[] = {7, 107}; expect(w, 2, 6); }
  members();
  { const int w[] = {31, 32, 41, 42, 33, 2, 133, 142, 141, 132, 131}; expect(w, 11, 7); }
  array();
  { const int w[] = {80, 81, 82, 90, 182, 181, 180}; expect(w, 7, 8); }
  delegating();
  { const int w[] = {77, 1, 2, 177}; expect(w, 4, 9); }
  printf("bad=%d\n", bad);
  return bad != 0;
}
