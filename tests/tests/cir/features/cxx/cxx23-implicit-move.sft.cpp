//type:rp
//options:--c++23:--c++23 -O2
//require:BACK_END_TYPE 3
/* cxx23.implicit_move: simpler implicit move: return of an id-expression naming an rvalue reference moves (P2266R3). */
extern "C" int printf(const char *, ...);
extern "C" int puts(const char *);
static int failures;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #__VA_ARGS__); } } while (0)
template <typename A, typename B> struct IsSame { static const bool v = false; };
template <typename A> struct IsSame<A, A> { static const bool v = true; };

static int moves, copies;
struct Mv {
  int v;
  Mv(int x) : v(x) {}
  Mv(const Mv &o) : v(o.v) { ++copies; }
  Mv(Mv &&o) : v(o.v) { ++moves; o.v = -1; }
};

static Mv pass(Mv &&m) { return m; }
static Mv &&fwd(Mv &&m) { return m; }
static Mv local(int x)
{
  Mv m(x);
  return m;
}
static Mv cond(bool b, Mv &&x, Mv &&y)
{
  if (b) return x;
  return y;
}
static auto deduced(Mv &&m) { return m; }

static void run()
{
  Mv a(1);
  Mv b = pass(static_cast<Mv &&>(a));
  CHECK(b.v == 1 && a.v == -1 && moves == 1 && copies == 0);
  Mv c(2);
  Mv &&r = fwd(static_cast<Mv &&>(c));
  CHECK(r.v == 2 && c.v == 2);
  Mv d = local(3);
  CHECK(d.v == 3);
  Mv e = cond(true, Mv(4), Mv(5));
  CHECK(e.v == 4);
  Mv f = cond(false, Mv(6), Mv(7));
  CHECK(f.v == 7);
  CHECK(copies == 0);
  Mv g = deduced(Mv(8));
  CHECK(g.v == 8 && copies == 0);
  CHECK((IsSame<decltype(deduced(Mv(1))), Mv>::v));
}

int main()
{
  run();
  if (failures == 0) puts("cxx23.implicit_move ok");
  return failures != 0;
}
