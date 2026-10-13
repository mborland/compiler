//options: --c++20 --g++ --gnu_version=140000
// Empty classes whose address is shared with another object: nothing may be
// written for them (Itanium: dsize 0).  Each case sets the neighbour byte
// first and prints it afterwards.
#include <new>
#include <tuple>
#include <memory>
extern "C" int printf(const char *, ...);
struct E { };
struct E2 { };
E mk() { return E(); }
void take(E) { }
void take2(E, char *p) { *p = *p; }

// A: [[no_unique_address]] empty member after a non-empty one (offset 0).
struct A { char x; [[no_unique_address]] E e; };
// B: member initialized from a function result, from a copy, from {}.
struct B1 { char x; [[no_unique_address]] E e; B1() : x('a'), e(mk()) { } };
struct B2 { char x; [[no_unique_address]] E e; B2(const B2 &o) : x(o.x), e(o.e) { } B2() : x('b') { } };
struct B3 { char x; [[no_unique_address]] E e; B3() : x('c'), e{} { } };
struct B4 { char x; [[no_unique_address]] E e; B4() : x('d'), e() { } };
struct B5 { char x = 'e'; [[no_unique_address]] E e; };           // default member init
struct B6 { char x; [[no_unique_address]] E e; B6(int) : x('f') { } B6() : B6(0) { } };
// C: empty base with a non-empty first member; defaulted special members.
struct C : E { char y; };
struct C2 : E, E2 { char y; };
struct C3 : C { };
// D: member after non-empty, copy/move assignment synthesized.
struct D { char x; [[no_unique_address]] E e; [[no_unique_address]] E2 e2; };
// An empty class whose only fields are empty bases (no data).
struct Tag { };
struct Tag2 : Tag { };
struct H { char x; [[no_unique_address]] Tag2 t; };
Tag2 mk2() { return Tag2(); }
int main() {
  { A a{'1', {}}; printf("A1 %c\n", a.x); }
  { A a{'2'}; printf("A2 %c\n", a.x); }
  { A a = {'3', E()}; printf("A3 %c\n", a.x); }
  { A a{'4', mk()}; printf("A4 %c\n", a.x); }
  { B1 b; printf("B1 %c\n", b.x); }
  { B2 b0; B2 b(b0); printf("B2 %c\n", b.x); }
  { B3 b; printf("B3 %c\n", b.x); }
  { B4 b; printf("B4 %c\n", b.x); }
  { B5 b; printf("B5 %c\n", b.x); }
  { B6 b; printf("B6 %c\n", b.x); }
  { A a{'5', {}}; E e; a.e = e; printf("A5 %c\n", a.x); a.e = mk(); printf("A5b %c\n", a.x); }
  { A a{'6', {}}; take(a.e); printf("A6 %c\n", a.x); take2(mk(), &a.x); printf("A6b %c\n", a.x); }
  { A a{'7', {}}; A b{'8', {}}; a = b; printf("A7 %c\n", a.x); a = static_cast<A &&>(b); printf("A7b %c\n", a.x); }
  { A a{'9', {}}; A b(a); printf("A8 %c\n", b.x); A c(static_cast<A &&>(a)); printf("A8b %c\n", c.x); }
  { C c; c.y = 'p'; C d; d.y = 'q'; c = d; printf("C1 %c\n", c.y); C e(c); printf("C2 %c\n", e.y); C f(static_cast<C &&>(c)); printf("C3 %c\n", f.y); }
  { C2 c; c.y = 'r'; C2 d; d.y = 's'; c = d; printf("C4 %c\n", c.y); C2 e(c); printf("C5 %c\n", e.y); }
  { C3 c; c.y = 't'; C3 d; d.y = 'u'; c = d; printf("C6 %c\n", c.y); C3 e(d); printf("C7 %c\n", e.y); }
  { D a{'v', {}, {}}; D b{'w', {}, {}}; a = b; printf("D1 %c\n", a.x); D c(a); printf("D2 %c\n", c.x); }
  { A a{'x', {}}; new (&a.e) E(); printf("N1 %c\n", a.x); new (&a.e) E{}; printf("N2 %c\n", a.x); }
  { A a{'x', {}}; E e0; new (&a.e) E(e0); printf("N4 %c\n", a.x); new (&a.e) E(mk()); printf("N5 %c\n", a.x); E &r = a.e; new (&r) E(r); printf("N6 %c\n", a.x); }
  { C c; c.y = 'm'; new (static_cast<E *>(&c)) E(); printf("N7 %c\n", c.y); E e0; new (static_cast<E *>(&c)) E(e0); printf("N8 %c\n", c.y); new (static_cast<E *>(&c)) E{}; printf("N9 %c\n", c.y); }
  { C c{{}, 'a'}; printf("G1 %c\n", c.y); C d{E{}, 'b'}; printf("G2 %c\n", d.y); C2 e{{}, {}, 'c'}; printf("G3 %c\n", e.y); C3 f{{{}, 'd'}}; printf("G4 %c\n", f.y); }
  { C c = C(); printf("V1 %d\n", (int)c.y); C *p = new C(); printf("V2 %d\n", (int)p->y); delete p; C q{}; printf("V3 %d\n", (int)q.y); }
  { H h{'h', {}}; Tag2 t2; h.t = t2; printf("H1 %c\n", h.x); h.t = mk2(); printf("H2 %c\n", h.x); new (&h.t) Tag2(); printf("H3 %c\n", h.x); new (&h.t) Tag2{}; printf("H4 %c\n", h.x); new (&h.t) Tag2(t2); printf("H5 %c\n", h.x); }
  { std::tuple<std::allocator<int>, int> t(std::allocator<int>(), 0x12345678); std::get<0>(t) = std::allocator<int>{}; printf("T1 %x\n", std::get<1>(t)); std::tuple<std::allocator<int>, int> u(t); std::get<0>(u) = std::get<0>(t); t = u; printf("T2 %x %x\n", std::get<1>(t), std::get<1>(u)); new (&std::get<0>(t)) std::allocator<int>(); printf("T3 %x\n", std::get<1>(t)); }
  { A *p = new A{'y', {}}; printf("N3 %c\n", p->x); delete p; }
  { A arr[2] = {{'a', {}}, {'b', {}}}; A arr2[2] = {}; printf("Z %c %c\n", arr[0].x, arr[1].x); }
  { char buf[2] = {'k', 'l'}; E *pe = new (buf) E(); (void)pe; printf("P %c%c\n", buf[0], buf[1]); }
  return 0;
}
