//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// A class first seen as a pointee (incomplete) and completed later has its
// real size when it is allocated: the size of the new expression and the
// record are those of the completed class.
template <class T> struct D;
template <class T> struct F { F(); D<T> *f; unsigned f0; };
struct I { int x; };
struct H { F<I> *p; };
template <class T> struct D { T d; D<T> *n; };
template <class T> F<T>::F() { f = new D<T>; }
H *m() { H *h = new H; h->p = new F<I>(); return h; }
