//type:fp
//options:--c++17 --g++ --gnu_version=150200
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// Supp key 16 (always_inline) on the members of a class template that are
// defined in the class and are neither inline nor constexpr, as libstdc++
// writes them with _GLIBCXX20_CONSTEXPR (empty before C++20): the front end
// has not marked such a member inline when the attribute is applied, and it
// used to drop the attribute (new_allocator::max_size, allocator's
// constructors and destructor).  A member of a class that is not a template
// and a function template are the control.
template<class T> struct A {
  __attribute__((always_inline)) A() { }
  __attribute__((always_inline)) A(const A &) { }
  __attribute__((always_inline)) ~A() { }
  __attribute__((always_inline)) unsigned long max() const { return 1; }
  __attribute__((always_inline)) constexpr unsigned long cmax() const {
    return 2;
  }
  __attribute__((always_inline)) unsigned long out() const;
  unsigned long plain() const { return 3; }
};
template<class T> unsigned long A<T>::out() const { return 4; }
struct N {
  __attribute__((always_inline)) unsigned long m() const { return 5; }
};
template<class T> __attribute__((always_inline)) T tf(T x) { return x; }
unsigned long use() {
  A<char> a;
  A<char> b(a);
  N n;
  return a.max() + b.cmax() + a.out() + a.plain() + n.m() + tf(1ul);
}
