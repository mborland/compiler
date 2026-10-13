//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// M4 slice S2: the record of a class with virtual bases.  The VIRTUAL_BASE
// fields follow the data members (in the front end's preorder, at their
// complete-object offsets); an empty virtual base has EMPTY_FOR_ABI; the data
// size of the record is the largest end of any field, a virtual base ending at
// its offset plus its nvsize (clang's dsize of the complete object): N below
// has dsize 28, not 32; CLASS_INFO has the front end's nvsize and the primary
// base, which is a virtual base for B, F, I (no storage of its own).
struct A { virtual void f() {} int a; };
struct B : virtual A { };
struct V1 { int i; };
struct L : virtual V1 { int l; char c; };
struct E0 {};
struct F : virtual E0 { int i; };
struct G { virtual void g() {} };
struct H : G, virtual E0 { char h; };
struct I : virtual G { };
struct J : virtual I { };
struct V2 : virtual V1 { int j; };
struct M : virtual V2 { int m; };
struct N : virtual V1, virtual V2 { char n; };
struct O : N { char o; };
struct P : virtual G { virtual void p() {} };
B b; L l; F f; H h; I i; J j; V2 v2; M m; N n; O o; P p;
int main() { return b.a + l.l + f.i + h.h + v2.j + m.m + n.n + o.o; }
