//type:rp
//options:--c++17:--c++17 -O2
//require:BACK_END_TYPE 3
/* cxx17.range_based_for_sentinel: range-based for where begin() and end() return different types. */
extern "C" int printf(const char *, ...);
extern "C" int puts(const char *);
static int failures;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #__VA_ARGS__); } } while (0)
struct Sentinel {};

struct CharIter {
  const char *p;
  char operator*() const { return *p; }
  CharIter &operator++() { ++p; return *this; }
};
bool operator!=(const CharIter &it, Sentinel) { return *it.p != 0; }
bool operator==(const CharIter &it, Sentinel) { return *it.p == 0; }

struct CString {
  const char *s;
  CharIter begin() const { return CharIter{s}; }
  Sentinel end() const { return Sentinel{}; }
};

struct CountIter {
  int cur;
  int operator*() const { return cur; }
  CountIter &operator++() { ++cur; return *this; }
};
struct CountEnd { int limit; };
bool operator!=(const CountIter &a, const CountEnd &e) { return a.cur < e.limit; }

struct Counter {
  int lo, hi;
  CountIter begin() const { return CountIter{lo}; }
  CountEnd end() const { return CountEnd{hi}; }
};

static void run()
{
  int n = 0;
  int sum = 0;
  for (char c : CString{"hello"}) { ++n; sum += c; }
  CHECK(n == 5 && sum == 'h' + 'e' + 'l' + 'l' + 'o');
  int total = 0;
  for (int v : Counter{3, 8}) total += v;
  CHECK(total == 3 + 4 + 5 + 6 + 7);
  int iters = 0;
  for (int v : Counter{5, 5}) { (void)v; ++iters; }
  CHECK(iters == 0);
  Counter c = {0, 100};
  int last = -1;
  for (auto v : c) { if (v == 10) break; last = v; }
  CHECK(last == 9);
  int odd = 0;
  for (auto v : c) { if (v % 2 == 0) continue; if (v > 9) break; odd += v; }
  CHECK(odd == 1 + 3 + 5 + 7 + 9);
}

int main()
{
  run();
  if (failures == 0) puts("cxx17.range_based_for_sentinel ok");
  return failures != 0;
}
