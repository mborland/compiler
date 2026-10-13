//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// M4 slice S2: a static local with a dynamic initializer in a constructor of a
// class with virtual bases has its STATIC_INIT in both bodies (C1 and C2, spec
// 12.3, K5a): one definition, one guard, the same name; so has the static
// temporary that a static reference extends (_ZGR...).
extern "C" int printf(const char *, ...);
struct W { int w; W(int x) : w(x) { printf("W(%d)\n", x); } ~W() { printf("~W\n"); } };
struct Body : virtual W {
  int b;
  Body() : W(7), b(0) {
    static W once(3);
    static const W &sref = W(8);
    b = once.w + sref.w;
  }
};
struct Derived : Body { Derived() : W(2) {} };
int main() { Body a; Derived d; return a.b + d.b; }
