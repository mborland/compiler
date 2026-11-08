//type:fp
//options:--c++17 --g++ --gnu_version=140000
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// A default member initializer that names another member through the implicit
// "this" in an aggregate initialization that is not constant (a local, a
// new-expression, a return) is diagnosed and the function is dropped, not
// written wrong: the writer has no "this" for an aggregate being initialized
// (cxx14.aggregate_nsdmi).  The same initializations are fine at namespace
// scope, where the front end folds them.
struct T {
  int a = 1;
  int b = a + 1;
};
T g1 = {5};
T g2 = {.a = 6};
int pos(int m) { T t = {m}; return t.b; }
int desig(int m) { T t = {.a = m}; return t.b; }
int heap(int m) { T *t = new T{m}; int r = t->b; delete t; return r; }
T ret(int m) { return T{m}; }
