//type:fp
//options:--c++20 --g++ --gnu_version=140000
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// A class whose only fields are empty bases has data size 0 (std::allocator<T>
// derives from an empty base): its assignment, value-initialization and
// constant initialization write nothing either (a tuple<allocator<int>, int>
// has the allocator at the address of the int).
struct Tag { };
struct Tag2 : Tag { };
struct H { char x; [[no_unique_address]] Tag2 t; };
inline void *operator new(decltype(sizeof 0), void *p) noexcept { return p; }
void f(H &h, const Tag2 &t) {
  h.t = t;
  new (&h.t) Tag2();
  new (&h.t) Tag2{};
}
