//options: --c++17
// A class conditional whose value is discarded (arms are temporaries, one
// may be a throw), and ::delete of a polymorphic object.
extern "C" int printf(const char *, ...); extern "C" void free(void *);
int n;
struct D { int id; D(int i) : id(i) { printf("D(%d)\n", i); } ~D() { printf("~D(%d)\n", id); } };
struct T { int v; };
T mkt(int i) { printf("mkt(%d)\n", i); return T{i}; }
D mkd(int i) { return D(i); }
void f(int c) {
  try {
    (c ? D(1) : throw 0);
    printf("after1\n");
    (c > 1 ? throw 1 : D(2));
    printf("after2\n");
  } catch (int e) { printf("caught %d\n", e); }
}
void g(int c) {
  (c ? D(3) : D(4));
  printf("g mid\n");
  (c ? mkt(5) : mkt(6));
  (c ? mkd(7) : mkd(8));
  printf("g end\n");
}
static int deleted;
void operator delete(void *p) noexcept { deleted = 1; printf("global delete\n"); ::free(p); }
struct V {
  virtual ~V() { printf("~V\n"); }
  void operator delete(void *p) { deleted = 2; printf("class delete\n"); ::free(p); }
};
struct W : V { ~W() { printf("~W\n"); } };
int main() {
  f(0); f(1); f(2);
  g(0); g(1);
  V *p = new W;
  ::delete p;
  printf("deleted=%d\n", deleted);
  return 0;
}
