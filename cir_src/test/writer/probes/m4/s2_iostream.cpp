//options:--c++17
// (the S2 writer test of the same name in tests/tests/cir/writer is this program)
// M4 slice S2, the shape of the iostream classes: basic_ios has a virtual
// destructor, basic_istream and basic_ostream derive from it virtually (they are
// polymorphic and their primary base is the virtual base), basic_iostream
// derives from both; the stream operators are member functions that call virtual
// functions of the virtual base through it.  A template instance, so the
// vtables, VTTs and typeinfo are linkonce_odr with COMDAT.
extern "C" int printf(const char *, ...);
template <class C> struct basic_ios {
  int state;
  basic_ios() : state(0) {}
  virtual ~basic_ios() {}
  virtual void flush() { printf("flush\n"); }
  void clear(int s) { state = s; }
  bool good() const { return state == 0; }
};
template <class C> struct basic_ostream : virtual basic_ios<C> {
  basic_ostream &put(C c) { printf("%c", (int)c); if (!this->good()) this->flush(); return *this; }
  basic_ostream &operator<<(int v) { printf("%d", v); this->clear(v); return *this; }
};
template <class C> struct basic_istream : virtual basic_ios<C> {
  int gcount_;
  basic_istream() : gcount_(0) {}
  int get() { gcount_++; return this->good() ? 1 : -1; }
};
template <class C> struct basic_iostream : basic_istream<C>, basic_ostream<C> {
  basic_iostream() {}
  ~basic_iostream() override { printf("~iostream\n"); }
};
int use(basic_ostream<char> &os, basic_istream<char> &is, basic_ios<char> &ios) {
  os.put('x') << 1 << 2;
  ios.flush();
  return is.get() + ios.good();
}
int main() {
  basic_iostream<char> io;
  basic_iostream<char> *p = new basic_iostream<char>;
  int n = use(io, io, io) + use(*p, *p, *p);
  delete p;
  return n == 0;
}
