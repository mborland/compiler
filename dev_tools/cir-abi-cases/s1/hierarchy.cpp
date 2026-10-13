// S1: class typeinfo kinds: __class_type_info (no bases),
// __si_class_type_info (one public non-virtual base at offset 0),
// __vmi_class_type_info (several bases, or a non-public one).
struct A { virtual void f(); int a; };
struct B : A { void f() override; };
struct C { virtual void g(); int c; };
struct D : B, C { void g() override; };
struct E : private A { void f() override; };
void A::f() {}
void B::f() {}
void C::g() {}
void D::g() {}
void E::f() {}
