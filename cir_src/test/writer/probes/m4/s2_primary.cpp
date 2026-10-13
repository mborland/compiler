//options:--c++17
struct N { virtual void nf(); };
void N::nf() {}
struct VN2 : virtual N {};
struct P { int i; };
struct W1 : VN2, virtual P { int w; };
struct VN3 : VN2 {};
struct W2 : VN3, virtual P { int w; };
struct Q1 : virtual N {};
struct Q2 : virtual N {};
struct W3 : Q1, Q2 { int w; };
W1 a; W2 b; W3 c;
extern "C" int printf(const char *, ...);
int off(void *base, void *p) { return (int)((char *)p - (char *)base); }
int main() {
  printf("%d %d %d %d %d %d\n", off(&a, &a.w), off(&b, &b.w), off(&c, &c.w), (int)sizeof(a), (int)sizeof(b), (int)sizeof(c));
  return 0;
}
