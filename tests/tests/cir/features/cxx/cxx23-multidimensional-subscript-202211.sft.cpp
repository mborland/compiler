//type:rp
//options:--c++23:--c++23 -O2
//require:BACK_END_TYPE 3
/* cxx23.multidimensional_subscript_202211: static operator[] and static operator(), and multi-argument operator[]. */
extern "C" int printf(const char *, ...);
extern "C" int puts(const char *);
static int failures;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #__VA_ARGS__); } } while (0)
struct Table {
  static int operator[](int i) { return i * i; }
  static int operator()(int a, int b) { return a * 10 + b; }
};
struct Grid {
  int cells[4][4];
  Grid()
  {
    for (int r = 0; r < 4; ++r)
      for (int c = 0; c < 4; ++c) cells[r][c] = r * 4 + c;
  }
  int &operator[](int r, int c) { return cells[r][c]; }
  const int &operator[](int r, int c) const { return cells[r][c]; }
};
struct Cube {
  int v[2][3][4];
  int &operator[](int a, int b, int c) { return v[a][b][c]; }
  static int zero() { return 0; }
};
struct Only {
  static int operator[]() { return 77; }
};

static void run()
{
  Table t;
  CHECK(t[3] == 9 && Table::operator[](4) == 16);
  CHECK(t(2, 5) == 25 && Table()(1, 2) == 12);
  CHECK(Table{}[5] == 25);
  Grid g;
  CHECK(g[1, 2] == 6 && g[3, 3] == 15);
  g[2, 1] = 100;
  CHECK(g.cells[2][1] == 100);
  const Grid &cg = g;
  CHECK(cg[0, 3] == 3);
  Cube c;
  c[1, 2, 3] = 9;
  CHECK(c.v[1][2][3] == 9);
  Only o;
  CHECK(o[] == 77);
  int (*fp)(int) = &Table::operator[];
  CHECK(fp(6) == 36);
}

int main()
{
  run();
  if (failures == 0) puts("cxx23.multidimensional_subscript_202211 ok");
  return failures != 0;
}
