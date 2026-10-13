/* The shape of boost::exception_detail::clone_impl<T>: a class template
   deriving from a user class T and from a virtual base with pure virtual
   functions.  Both translation units instantiate clone_impl<my_error> (the
   vtable, VTT and structors are weak symbols in COMDAT groups that the linker
   merges) and the clone of one is used by the other.  (boost's rethrow throws
   *this: exceptions come with S4.) */
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
clone_base const *make_a(int c);  // a.cpp
clone_base const *make_b(int c);  // b.cpp
int twice_clone(clone_base const *p);  // b.cpp: clones, gets, deletes
