//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// M4 slice S2: a virtual base field is EMPTY_FOR_ABI (flag 67) iff its class is
// empty, or it is the primary virtual base of the class or of one of its
// bases, direct or indirect (clang's hasOwnStorage): it then lives inside that
// base's storage, at the base's offset. N is the primary virtual base of VN2
// (so W1 gives it no storage: w is at 8, as g++), of VN3 through VN2 (W2, two
// levels), and of Q1 and Q2, the first of which owns it (W3).  P is an ordinary
// virtual base.
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
