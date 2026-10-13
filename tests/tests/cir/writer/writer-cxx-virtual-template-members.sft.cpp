//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// M4 slice S1: key functions and TU-locality of classes that belong to templates.
// A member class of a class template and an instance of a partial
// specialization have no key function (linkonce_odr, COMDAT), as an instance of
// the primary template; an explicit specialization is an ordinary class (key
// function defined here); with a TU-local type argument (an unnamed-namespace
// class) the instance is TU-local (internal vtable, TU_LOCAL typeinfo).  The
// local classes: of an inline function (linkonce_odr, no TU_LOCAL), of an
// ordinary function (internal, TU_LOCAL: g++ writes "*" for it).
template <class T> struct Outer {
  struct Inner { virtual void f(); virtual ~Inner() {} };
  struct Inl { virtual void g() {} };
};
template <class T> void Outer<T>::Inner::f() {}
template <class T> struct PS { virtual void h(); };
template <class T> struct PS<T *> { virtual void h2() {} virtual void k(); };
template <class T> void PS<T *>::k() {}
template <> struct PS<long> { virtual void h(); };
void PS<long>::h() {}
namespace { struct Hid {}; }
Outer<int>::Inner a;
Outer<int>::Inl b;
PS<int *> c;
PS<char> d;
PS<long> e;
Outer<Hid>::Inner f;
PS<Hid *> g;
struct B { virtual ~B() {} };
int ext() { struct Loc : B {}; Loc l; return 0; }
inline int inl() { struct ILoc : B {}; ILoc l; return 0; }
static int stat() { struct SLoc : B {}; SLoc l; return 0; }
int main() { return ext() + inl() + stat(); }
