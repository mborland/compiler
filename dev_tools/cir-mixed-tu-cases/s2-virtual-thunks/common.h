/* A function of a virtual base overridden in a derived class: V's slot in
   D's vtable is a virtual thunk (_ZTv0_n24_N1D1hEi: the this adjustment is
   read from the vtable, since V's offset in D is not constant).  D's key
   function is own (a.cpp, with the vtable); the overrider h is defined in
   b.cpp only, so its thunk must come with it, as in s1-split-overriders.
   The other overrider, k, is defined in a.cpp together with the vtable. */
struct V {
  virtual int h(int x);
  virtual int k();
  int v = 5;
};
struct D : virtual V {
  D();
  virtual int own();
  int h(int x) override;
  int k() override;
  int d = 6;
};
D *make_d();       // a.cpp
int call_h(V *p);  // a.cpp: p->h(2) + p->k()
