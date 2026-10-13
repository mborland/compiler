//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// A class conditional whose value is discarded is a void TERN: each arm is
// the object (built in the temporary of the conditional when it has one, else
// a temporary of its own that is dropped), with a throw as an arm, with and
// without a destructor.
struct D { ~D(); };
struct T { int v; };
T mkt(int);
D mkd(int);
void f(int c) {
  (c ? D() : throw 0);
  (c > 1 ? throw 1 : D());
  (c ? T() : throw 2);
  (c ? mkt(5) : mkt(6));
  (c ? mkd(7) : mkd(8));
}
