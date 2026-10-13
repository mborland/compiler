//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// new T[n]{...} with a run-time n: before the allocation n < k (the number of
// listed elements) calls __cxa_throw_bad_array_new_length; the first k
// elements are initialized from the list (as a pointer to the array of k), the
// destruction of those is a DEACTIVATABLE eh cleanup while the other n - k are
// value-initialized (a loop of the default constructor with ARRAY_PARTIAL_DTOR,
// or of INIT_ZERO), then the cleanup ends.
struct S { int a; S(); S(int); ~S(); };
struct P { int x, y; };
S *f1(int n) { return new S[n]{S(1), S(2)}; }
P *f2(int n) { return new P[n]{{1, 2}, {3, 4}, {5, 6}}; }
int *f3(int n) { return new int[n]{1, 2, 3}; }
int *f4(int n) { return new int[n]{}; }
