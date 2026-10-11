//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// A function type that mentions the record being built (by value as the
// return type or a parameter, in a member that points to a function) must not
// start the record again.
struct S { S (*clone)(); int x; };
S g;
struct S2 { void (*f)(S2); };
S2 g2;
struct N { N (*next)(N); N *p; int v; };
N n;
template<class T> struct Fn { T (*f)(); };
struct Node { Fn<Node> next; int k; };
Node nd;
int main() { return g.x + n.v + nd.k; }
