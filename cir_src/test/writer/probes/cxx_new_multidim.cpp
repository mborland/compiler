// new T[n][K]: the count of elements includes the inner bounds; class
// elements get their constructor and destructor per element; value
// initialization zeroes members first.  Run and compare with g++.
extern "C" int printf(const char *, ...);
extern "C" void *malloc(unsigned long);
extern "C" void free(void *);
static unsigned long last_size;
void *operator new[](unsigned long n) {
  last_size = n;
  return malloc(n);
}
void operator delete[](void *p) noexcept { free(p); }
struct A {
  int v;
  static int live;
  A() : v(++live) {}
  ~A() { printf("~A %d\n", v); live--; }
};
int A::live;
struct B { B() {} int x; int y; };
struct Z { int a; B b; };
int main(int argc, char **) {
  int n = argc + 1;
  int (*p)[4] = new int[n][4];
  printf("int size %lu\n", last_size);
  for (int i = 0; i < n; i++)
    for (int j = 0; j < 4; j++) p[i][j] = i * 4 + j;
  printf("%d %d\n", p[1][3], p[n - 1][0]);
  delete[] p;
  A (*q)[3] = new A[n][3];
  printf("A size %lu live %d\n", last_size, A::live);
  printf("%d %d\n", q[0][0].v, q[n - 1][2].v);
  delete[] q;
  printf("live %d\n", A::live);
  A (*r)[3] = new A[2][3];
  printf("A const size %lu live %d\n", last_size, A::live);
  delete[] r;
  Z *z = new Z[3]();
  int sum = 0;
  for (int i = 0; i < 3; i++) sum += z[i].a + z[i].b.x + z[i].b.y;
  printf("z %d\n", sum);
  delete[] z;
  return 0;
}
