// A repeated non-virtual base: two subobjects, two override lists.
struct A { virtual void f(); virtual void g(); };
struct B : A { };
struct C : A { void g(); };
struct D : B, C { void f(); };                                   // f overrides both A::f
struct E : B, C { };                                             // ambiguous final overriders: unused
struct F : virtual A { void f(); };
struct G : F, virtual A { };
void A::f() {}
void A::g() {}
void C::g() {}
void D::f() {}
void F::f() {}
