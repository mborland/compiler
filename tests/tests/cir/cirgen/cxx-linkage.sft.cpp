//type:rp
//options:--c++17
//require:BACK_END_TYPE 3
// Linkage: inline functions (vague linkage), function and class templates
// instantiated for two types, an explicit specialization, an anonymous
// namespace (internal), static functions, member functions defined in the class
// and out of it.
extern "C" int printf(const char *, ...);
int ext(int x) { return x; }
static int stat(int x) { return x + 1; }
inline int inl(int x) { return x + 2; }
static inline int sinl(int x) { return x + 3; }
namespace { int anon(int x) { return x + 4; } inline int anoninl(int x) { return x + 5; }
            struct Priv { int p; int get() const { return p; } }; }
namespace ns { inline int twice(int x) { return 2 * x; } }
template <class T> T tf(T x) { return x + x; }
template <> int tf<int>(int x) { return x + 100; }
template <class T> struct TS { T v; T get() const { return v; } T twice() const { return v + v; } };
template <> struct TS<char> { char v; int get() const { return v + 1000; } };
struct S { int m() { return 1; } int n(); static int sm() { return 2; } };
int S::n() { return 3; }
inline int sn(S &s) { return s.n(); }
int main() {
  S s;
  TS<int> a{1};
  TS<long> l{2};
  TS<char> c{3};
  Priv pr{6};
  int bad = 0;
  int r1 = ext(1) + stat(1) + inl(1) + sinl(1) + anon(1) + anoninl(1) + ns::twice(2);
  int r2 = tf(1) + (int)tf(2L) + (int)tf(1.5f) + a.get() + (int)l.get() + c.get() + a.twice() + (int)l.twice();
  int r3 = s.m() + S::sm() + s.n() + sn(s) + pr.get();
  if (r1 != 1 + 2 + 3 + 4 + 5 + 6 + 4) bad |= 1;
  if (r2 != 101 + 4 + 3 + 1 + 2 + 1003 + 2 + 4) bad |= 2;
  if (r3 != 1 + 2 + 3 + 3 + 6) bad |= 4;
  printf("%d %d %d bad=%d\n", r1, r2, r3, bad);
  return bad;
}
