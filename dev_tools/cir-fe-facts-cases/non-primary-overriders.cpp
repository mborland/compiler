// Overriders of functions of non-primary bases get a new slot in the
// primary vtable.
struct X { virtual void g(); };
struct Y { virtual void h(); virtual void k(); };
struct Z : X, Y { void g(); void h(); virtual void z(); };       // h: new slot; k: not overridden
struct Z2 : X, Y { void k(); };                                  // only a secondary overrider
struct Z3 : Y, X { void g(); void k(); };
void X::g() {}
void Y::h() {}
void Y::k() {}
void Z::g() {}
void Z::h() {}
void Z::z() {}
void Z2::k() {}
void Z3::g() {}
void Z3::k() {}
