// Where the implicit virtual destructor and the virtual functions of a
// derived class get their numbers.
struct P { virtual ~P(); virtual void p1(); };
struct Q : P { virtual void a(); virtual void b(); };            // implicit ~Q: P's slot
struct R { virtual void a(); };
struct S : R { virtual ~S(); virtual void b(); };                // ~S: new, before b
struct T : R { virtual void b(); virtual ~T(); virtual void c(); };  // ~T: between
struct U { virtual void a(); virtual void b(); };
struct V : U { virtual void c(); };                              // no dtor at all
struct W { };
struct X : W { virtual void a(); };                              // implicit dtor not virtual
void P::p1() {}
P::~P() {}
void R::a() {}
S::~S() {}
void S::b() {}
void T::b() {}
T::~T() {}
void T::c() {}
void U::a() {}
void V::c() {}
void X::a() {}
