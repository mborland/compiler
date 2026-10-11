struct P { long a, b; };
template <class T> struct W { T v; W(T t) : v(t) {} ~W() {} };
template struct W<P>;
struct I { long s; I(P p) : s(p.a) {} ~I() {} };
void use() { P p{1,2}; I i(p); }
