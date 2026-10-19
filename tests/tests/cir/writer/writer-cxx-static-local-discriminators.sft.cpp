//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// The variable of a static anonymous union is named after its first member
// and takes its discriminator in the sequence of the entities of that name
// (_ZZ2h1vE1i, _ZZ2h1vE1i_0, ...), in declaration order, in whichever order
// it comes with a static local of the same name; the names are those of
// g++ and clang.  Unnamed local types do not take part in the sequence.
inline int h1() { static union { int i; }; int r = i; { static int i = 2; r += i; } return r; }
inline int h2() { static int i = 1; int r = i; { static union { int i; }; r += i; } return r; }
inline int h3(bool c) {
  if (c) { static union { int i; }; return i; }
  else { static union { int i; }; return i; }
}
inline int h4() { static union { int a; }; static union { int b; }; return a + b; }
inline int h5() {
  struct { int k; } u1;
  static union { int i; };
  struct { int m; } u2;
  u1.k = 1; u2.m = 2;
  return i + u1.k + u2.m;
}
inline int h6() { static union { union { int n; }; float g; }; return n; }
inline int h7() {
  static union { int i; };
  static union { int i2; };
  { static union { int i; }; return i + i2; }
}
template<class T> T h9(T x) { static union { T v; }; static T v2; v2 = x; return v + v2; }
int use() {
  return h1() + h2() + h3(true) + h3(false) + h4() + h5() + h6() + h7() +
         h9(1) + h9(2.0);
}
