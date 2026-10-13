//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// Covariant returns: for each override whose return class is derived from the
// overridden function's return class, the layout facts of the returned class
// come with the vtable facts of the class (the library derives the adjustment
// thunk from them), and its vtable facts too when the path from it to the
// overridden return class goes through a virtual base.  Three shapes: a
// non-dynamic D2 : R1, R2 returned for R2* (a +offset thunk), a dynamic D3
// whose constructor and destructor are defined in another translation unit,
// and D4 which has R2 as a virtual base.
struct R1 { int r1; };
struct R2 { int r2; };
struct D2 : R1, R2 { int d2; };
struct E3 { virtual ~E3(); virtual void e3(); };
struct R3 { virtual ~R3(); int r3; };
struct D3 : E3, R3 { D3(); ~D3(); };
struct R4 { int r4; };
struct D4 : virtual R4 { int d4; };
struct B1 { virtual R2 *f() { return 0; } virtual R3 *g() { return 0; } virtual R4 &h();
            virtual ~B1() {} };
struct C1 : B1 {
  D2 *f() override;
  D3 *g() override;
  D4 &h() override;
};
D2 *C1::f() { return 0; }
D3 *C1::g() { return 0; }
D4 &C1::h() { static D4 *p = 0; return *p; }
