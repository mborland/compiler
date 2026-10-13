//type:fp
//options:--c++20 --g++ --gnu_version=140000
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// The value-initialization of an empty class through a pointer (placement
// new of E() or E{}) writes nothing: its byte may belong to a member that
// shares its address ([[no_unique_address]] member, empty base).
struct E { };
struct A { char x; [[no_unique_address]] E e; };
inline void *operator new(decltype(sizeof 0), void *p) noexcept { return p; }
void f(A &a, char *buf) {
  new (&a.e) E();
  new (&a.e) E{};
  new (buf) E();
}
