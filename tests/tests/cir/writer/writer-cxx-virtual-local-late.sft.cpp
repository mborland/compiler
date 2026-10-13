//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// A local polymorphic class whose constructor is defined after the region of
// its function ended (mk<L> is instantiated at the end of the translation
// unit, as std::make_unique<L> is): the vtable of L was not used when the
// region ended, so it is decided (and its typeinfo) when the constructor is
// translated, in that function's frame.
struct I { virtual int f() = 0; virtual ~I() {} };
template <class T> T *mk() { return new T; }
auto make() {
  struct L : I { int f() override { return 3; } };
  return mk<L>();
}
int main() { I *p = make(); return p->f() - 3; }
