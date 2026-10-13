// S1: a class template with virtual functions: the vtable, typeinfo and
// typeinfo name of every instantiation are weak symbols in COMDAT groups.
template <class T>
struct Box {
  virtual T get() const { return value; }
  virtual void set(T v) { value = v; }
  virtual ~Box() {}
  T value;
};
template <class T>
struct Derived : Box<T> {
  T get() const override { return this->value + 1; }
};
int f() { Derived<int> d; d.set(1); return d.get(); }
double g() { Box<double> b; return b.get(); }
// A class that is not a template, so that this file compares at least one
// vtable (clang names Box<int> and Box<double> alike: see ABI_KNOWN_LIMITS).
struct Plain {
  virtual int p();
  int v;
};
int Plain::p() { return v; }
