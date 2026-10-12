//type:fp
//options:-w --c++03 --exceptions --pending_instantiations 200
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// A type of nested template arguments that share their parts (A<A<T, T>,
// A<T, T>>, 100 deep) is a tree of exponential size: the walk that decides
// whether an entity is local to the translation unit must queue each class
// once (gnats-26324 ran out of memory).
template<typename, typename> struct A;
template<typename T, int N>
struct C {
  typedef typename C<A<T, T>, N - 1>::type type;
};
template<typename T>
struct C<T, 0> {
  typedef int type;
};
C<int, 100>::type t;
template<typename T, typename U> struct X { };
template<typename T, int D>
struct M { typedef typename M<X<T, T>, D-1>::type type; };
template<typename T>
struct M<T, 0> { typedef T type; };
M<int, 60>::type *mp;
int use() { return mp == 0; }
