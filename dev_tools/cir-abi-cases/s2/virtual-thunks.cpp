// S2: virtual thunks (_ZTv0_n24_...): V's functions overridden in D, whose
// virtual base V is at a variable offset; a covariant return through the
// virtual base (_ZTcv...); thunks of a destructor.
struct V {
  virtual int h(int x);
  virtual int k();
  virtual ~V();
  int v;
};
struct R { int r; };
struct S : virtual R { int s; };
struct W {
  virtual R *get();
};
struct D : virtual V, W {
  D();
  virtual int own();
  int h(int x) override;
  int k() override;
  S *get() override;
  ~D() override;
  int d;
};
int V::h(int x) { return x; }
int V::k() { return 1; }
V::~V() {}
R *W::get() { return 0; }
D::D() : d(1) {}
int D::own() { return d; }
int D::h(int x) { return x + d + v; }
int D::k() { return d; }
S *D::get() { return 0; }
D::~D() {}
int call(V *p, W *w) { return p->h(1) + p->k() + w->get()->r; }
