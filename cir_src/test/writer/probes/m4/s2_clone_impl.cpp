//options:--c++17
// (the S2 writer test of the same name in tests/tests/cir/writer is this program)
// M4 slice S2, the shape of boost::exception_detail::clone_impl: a class
// template derived from its argument T and, virtually, from clone_base; the
// argument has a polymorphic base of its own.  clone_impl<T> has a primary base
// T (the first non-virtual dynamic base) and a virtual base, so its layout
// facts follow its body and need T's CLASS_INFO first.
extern "C" int printf(const char *, ...);
struct clone_base {
  virtual clone_base const *clone() const = 0;
  virtual void rethrow() const = 0;
  virtual ~clone_base() {}
};
struct error_info { int code; virtual ~error_info() {} };
struct my_error : error_info { int detail; my_error() : detail(1) { code = 7; } };
template <class T> struct clone_impl : T, virtual clone_base {
  explicit clone_impl(const T &x) : T(x) {}
  ~clone_impl() override {}
  clone_base const *clone() const override { return new clone_impl(*this); }
  void rethrow() const override { printf("rethrow %d\n", this->code); }
};
int use(const clone_base *c) { c->rethrow(); return c->clone() != 0; }
int main() {
  my_error e;
  clone_impl<my_error> ci(e);
  clone_impl<error_info> ci2(e);
  return use(&ci) + use(&ci2) - 2;
}
