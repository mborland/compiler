//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// esk_nonnull_ptrs on TY_FUNC: bit 0 is a reference result, bit i+1 marks
// parameter i (the object parameter of a non-static member function is
// parameter 0) when it is a reference or the object pointer.  The mask is
// part of the type: f(int *) and f(int &) have different function types.
struct S {
  int v;
  S();
  int get() const;
  void set(int x);
  static int stat(int &r);
  virtual void vf(const S &o);
  S &self();
};
int &ref_result(int &a, int *p, int b);
int &&rref(int &&r);
int use_ref(S &s, const S &t, int &&r);
void over(int *p);
void over(int &r);
void pointer_params(int *p, const int *q, void (*fp)(int &));
int call_fp(void (*fp)(int &), int &x) { fp(x); return 0; }
int (S::*pmf)() const = &S::get;
int call_pmf(S &s) { return (s.*pmf)(); }
void call_virtual(S &s, const S &o) { s.vf(o); }
S &chain(S &s) { return s.self(); }
int calls(S &s, int &r, int *p) {
  over(p);
  over(r);
  return ref_result(r, p, 1) + use_ref(s, s, static_cast<int &&>(r)) + S::stat(r);
}
