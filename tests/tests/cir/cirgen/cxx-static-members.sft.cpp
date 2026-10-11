//type:rp
//options:--c++17
//require:BACK_END_TYPE 3
// Static data members and static member functions: defined with an
// initializer, tentative, in-class constants, a constexpr member, members of a
// class template (constant initialized), namespace-scope variables, and a
// function-local static with a constant initializer.
extern "C" int printf(const char *, ...);
struct S {
  static int defd;
  static int uninit_defd;
  static const int sc = 3;
  static constexpr int scx = 4;
  static int get() { return defd; }
  static int bump() { return ++defd; }
  int v;
  int sum() const { return v + defd; }
};
int S::defd = 11;
int S::uninit_defd;
template <class T> struct TS { static int tm; static constexpr int tc = 5; };
template <class T> int TS<T>::tm = 12;
namespace N { int nd = 3; extern int later; }
int N::later = 8;
int tent;
int counter() { static int k = 5; k += 1; return k; }
int main() {
  S s;
  s.v = 4;
  int bad = 0;
  if (S::get() != 11 || s.sum() != 15) bad |= 1;
  S::bump();
  if (S::defd != 12 || s.get() != 12) bad |= 2;
  if (S::sc + S::scx + S::uninit_defd != 7) bad |= 4;
  TS<int>::tm += 1;
  if (TS<int>::tm != 13 || TS<long>::tm != 12 || TS<char>::tc != 5) bad |= 8;
  if (N::nd + N::later + tent != 11) bad |= 16;
  if (counter() != 6 || counter() != 7) bad |= 32;
  printf("%d %d %d bad=%d\n", S::defd, TS<int>::tm, counter(), bad);
  return bad;
}
