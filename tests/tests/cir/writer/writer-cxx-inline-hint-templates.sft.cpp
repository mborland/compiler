//type:fp
//options:--c++17 --g++ --gnu_version=140000
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// INLINE_HINT (0x200) on instances of templates and on friends, each checked
// against clangir's inlinehint.  An instance takes the hint from any
// declaration of its template that has the inline specifier: a redeclaration
// that adds it after the instance was made (sb), a member defined out of class
// with it (C<T>::m and C2<T>::m, with and without an earlier instantiation).
// An explicit specialization is a declaration of its own: it has the hint only
// if it has the specifier itself (sa has none, sa2 has it).  A friend defined
// in its class has the hint with the specifier (fr_inl, ftr_inl) and not
// without (fr_in, ftr).
template<class T> inline int sa(T) { return 1; }
template<> int sa<int>(int) { return 2; }
template<class T> int sa2(T) { return 1; }
template<> inline int sa2<int>(int) { return 2; }
template<class T> int sb(T);
int b_use() { return sb(1); }
template<class T> inline int sb(T) { return 1; }
template<class T> inline int sb2(T);
template<class T> int sb2(T) { return 1; }
int b2_use() { return sb2(1); }
template<class T> struct C {
  int m(T);
  int n(T) { return 1; }
  inline int o(T);
};
int c_use() { C<int> c; return c.m(1) + c.n(1) + c.o(1); }
template<class T> inline int C<T>::m(T) { return 1; }
template<class T> inline int C<T>::o(T) { return 1; }
template<class T> struct C2 { int m(T); };
template<class T> inline int C2<T>::m(T) { return 1; }
int c2_use() { C2<int> c; return c.m(1); }
struct F {
  friend int fr_in(F) { return 1; }
  friend inline int fr_inl(F) { return 1; }
};
int f_use() { F f; return fr_in(f) + fr_inl(f); }
template<class T> struct FT {
  friend int ftr(FT) { return 1; }
  friend inline int ftr_inl(FT) { return 1; }
};
int ft_use() { FT<int> f; return ftr(f) + ftr_inl(f); }
int sa_use() { return sa(1) + sa(1.0) + sa2(1); }
