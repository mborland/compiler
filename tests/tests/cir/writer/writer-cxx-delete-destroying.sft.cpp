//type:fp
//options:--c++20
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// A destroying operator delete (C++20): without a virtual destructor
// `delete p` is DELETE DESTROYING (0x20) with no destructor and the size if
// the function takes one; with a virtual destructor it is DELETE VIRTUAL and
// the deleting destructor calls the destroying delete (STRUCTOR_DELETING flag
// DESTROYING 0x04, the parameter count includes the empty tag); ::delete of a
// virtual class is VIRTUAL|GLOBAL.
namespace std {
struct destroying_delete_t { explicit destroying_delete_t() = default; };
inline constexpr destroying_delete_t destroying_delete{};
typedef decltype(sizeof 0) size_t;
}
extern "C" void free(void *);
struct NV {
  ~NV();
  void operator delete(NV *p, std::destroying_delete_t) { p->~NV(); free(p); }
  int x;
};
struct V {
  virtual ~V();
  void operator delete(V *p, std::destroying_delete_t);
  int x;
};
struct S { ~S(); void operator delete(S *p, std::destroying_delete_t, std::size_t sz); };
void dn(NV *p) { delete p; }
void dv(V *p) { delete p; }
void ds(S *p) { delete p; }
void gv(V *p) { ::delete p; }
