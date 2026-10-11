// lvalue results of ++x, --x and assignments, reference casts, function
// references, copies of const class objects, anonymous union members.
extern "C" int printf(const char *, ...);
int c = 5;
int tr(int v) { printf("tr %d\n", v); return v; }
char a1 = 'a', b1 = 'b';
char &pick(int i) { return i ? a1 : b1; }
void fn() { printf("fn\n"); }
int five() { return 5; }
int (&five_ref)() = five;
template <class T> void call(const T &f) { f(); }
struct S { unsigned v; };
const static S k1 = { 11 };
const S k2 = { (unsigned)five() };
struct U { int a; union { int b; double d; }; };
int main() {
  --c = tr(1);
  printf("c %d\n", c);
  (tr(2), --c) = tr(3);
  printf("c %d\n", c);
  ++c += 2;
  printf("c %d\n", c);
  int x = 4, y = 9;
  int &r = (x = y);
  r++;
  printf("%d %d\n", x, y);
  ((x = 3) = 4);
  printf("x %d\n", x);
  (tr(0) ? pick(1) : pick(0)) = 'z';
  printf("%c%c\n", a1, b1);
  float f = 1.5f;
  int bits = (int &)f;
  printf("%x\n", bits);
  (unsigned &)x = 77u;
  printf("x %d\n", x);
  void (&fr)() = fn;
  fr();
  void (*fp)() = fr;
  fp();
  call<void()>(fn);
  printf("%d\n", five_ref());
  S a = k1, b = k2;
  printf("%u %u\n", a.v, b.v);
  U u = { 1, { 2 } };
  U *pu = &u;
  pu->b = 8;
  u.d = 2.5;
  printf("%d %g\n", u.a, pu->d);
  return 0;
}
