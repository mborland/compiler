//type:rp
//options:--c++17
//require:BACK_END_TYPE 3
// Classes passed and returned by value that cannot be copied (a deleted copy
// constructor): the object is built where it ends up (guaranteed elision),
// through returns, call chains, temporaries, conditional returns and by-value
// parameters.
extern "C" int printf(const char *, ...);
struct NC {
  int x;
  int y;
  NC(const NC &) = delete;
  int sum() const { return x + y; }
};
NC mk(int v) { return NC{v, v + 1}; }
NC mk2(int v) { return mk(v * 2); }
NC pick(int v) { if (v > 2) return mk(v * 3); return mk(v); }
int take(NC n) { return n.x * 10 + n.y; }
int viatemp(int v) { return mk(v).sum() + take(mk(v + 1)); }
int use(int v) { NC a = mk2(v); NC b = pick(v); return a.sum() * 100 + b.sum(); }
int bad = 0;
void check(int got, int want) { if (got != want) { bad = bad * 10 + 1; printf("got %d want %d\n", got, want); } }
int main() {
  check(use(5), 21 * 100 + 31);
  check(use(1), 5 * 100 + 3);
  check(viatemp(2), 5 + 34);
  check(take(mk(4)), 45);
  check(mk2(3).sum(), 13);
  NC c = mk(8);
  check(c.sum() + take(mk(1)), 17 + 12);
  printf("%d %d %d bad=%d\n", use(5), viatemp(2), c.x, bad);
  return bad;
}
