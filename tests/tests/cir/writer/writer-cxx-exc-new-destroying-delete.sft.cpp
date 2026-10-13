//type:fp
//options:--c++20
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// The front end records a destroying operator delete as the function that
// frees the storage of a new expression when the initialization throws: it
// cannot be called with the pointer and a size, so the function is dropped
// with a reason.
namespace std {
typedef decltype(sizeof 0) size_t;
struct destroying_delete_t { explicit destroying_delete_t() = default; };
}
void operator delete(void *) noexcept;
void *operator new(std::size_t);
struct A {
  A() { throw 42; }
  ~A() { }
  void operator delete(void *p) { ::operator delete(p); }
  void operator delete(A *p, std::destroying_delete_t) {
    p->~A();
    ::operator delete(p);
  }
};
void f() { new A; }
