//type:fp
//options:--c++20
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// A destroying operator delete is diagnosed by the writer (the class's virtual
// destructor needs its D0): the functions that need its vtable facts are
// dropped.
namespace std { struct destroying_delete_t { explicit destroying_delete_t() = default; }; inline constexpr destroying_delete_t destroying_delete{}; }
struct DD {
  virtual ~DD() {}
  void operator delete(DD *p, std::destroying_delete_t) { p->~DD(); }
};
struct Plain { virtual void f() {} };
void use(Plain *p) { p->f(); }
int main() { DD d; Plain p; use(&p); return 0; }
