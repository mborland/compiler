//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// A placement operator delete that the program declares itself is called when
// the initialization of the object throws (gh-176); the standard one from a
// system header frees nothing and has no cleanup.
inline void *operator new(decltype(sizeof 0), void *p) noexcept { return p; }
int deletes;
inline void operator delete(void *, void *) noexcept { ++deletes; }
struct A { A(); };
void f(void *p) { ::new (p) A(); }
