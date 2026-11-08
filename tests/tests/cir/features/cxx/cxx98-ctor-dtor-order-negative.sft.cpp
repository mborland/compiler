//type:fn
//options:--c++11
//require:BACK_END_TYPE 3
/* A temporary that a mem-initializer binds to a reference member is lifetime
   extended outside an initialization: the CIR back end diagnoses it and drops
   the constructor.  The same temporary bound to a local reference is fine. */
struct T {
  int v;
  T(int x) : v(x) {}
  ~T() {}
};
struct R {
  const T &t;
  R(int x) : t(T(x)) {}
};
int use(int x)
{
  R r(x);
  return r.t.v;
}
int fine(int x)
{
  const T &r = T(x);
  return r.v;
}
