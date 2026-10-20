//type:fp
//options:--c++17 --g++ --gnu_version=140000
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// INLINE_HINT (0x200), with the INLINE_HINTS feature bit: set for a function
// declared with the inline specifier (also in a class, on a template and
// when the specifier is on a later declaration) and for a lambda's call
// operator, conversion function and static invoker; not for a function that
// is only inline because it is defined in its class, is constexpr, is an
// implicit member or is a template instance without the specifier.  INLINE
// keeps its own meaning (an inline function).
inline int expl(int x) { return x; }
int plain(int x) { return x; }
constexpr int cx(int x) { return x; }
constexpr inline int cx_inline(int x) { return x; }
struct S {
  int in_class(int x) { return x; }
  inline int in_class_inline(int x) { return x; }
  int out(int);
  inline int out_inline(int);
  static int st() { return 1; }
};
int S::out(int x) { return x; }
inline int S::out_inline(int x) { return x; }
template<class T> int tmpl(T) { return 1; }
template<class T> inline int tmpl_inline(T) { return 1; }
inline int later(int);
int later(int x) { return x; }
struct D { D() = default; int z; };
extern inline __attribute__((gnu_inline)) int gi(int x) { return x; }
int use() {
  auto l = [](int x) { return x; };
  S s;
  D d;
  return expl(1) + plain(2) + cx(3) + cx_inline(4) + s.in_class(5) +
         s.in_class_inline(6) + s.out(7) + s.out_inline(8) + S::st() +
         tmpl(1) + tmpl_inline(2) + later(3) + gi(1) + l(3);
}
