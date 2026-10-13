// S2: the shape of boost::exception_detail::clone_impl<T>: a class template
// deriving from a user class and from a virtual base with pure virtual
// functions; the vtable, VTT, typeinfo and structors are weak in COMDAT
// groups.
struct clone_base {
  virtual clone_base const *clone() const = 0;
  virtual int get() const = 0;
  virtual ~clone_base() {}
};
struct my_error {
  explicit my_error(int c) : code(c) {}
  virtual ~my_error() {}
  virtual int what() const { return code; }
  int code;
};
template <class T>
struct clone_impl : T, virtual clone_base {
  explicit clone_impl(T const &x) : T(x) {}
  clone_base const *clone() const override { return new clone_impl(*this); }
  int get() const override { return this->what() + 1; }
};
clone_base const *make(int c) {
  my_error e(c);
  return new clone_impl<my_error>(e);
}
int use(clone_base const *p) {
  clone_base const *q = p->clone();
  int r = q->get();
  delete q;
  return r;
}
