//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// The definition of a virtual function of a dynamic class sends the vtable facts
// of its class (without using the vtable), so that the library emits the
// thunks to it: C's key function f is defined in another translation unit,
// this one defines only g, which another translation unit's vtable of C
// refers to through the this-adjusting thunk _ZThn16_N1C1gEi (non-primary base
// A2); the covariant h likewise.
struct R0 { virtual ~R0() {} };
struct R1 : R0 {};
struct A1 { virtual int f() { return 1; } int a1; };
struct A2 { virtual int g(int) { return 2; } virtual R0 *h() { return 0; } int a2; };
struct C : A1, A2 {
  int f() override;
  int g(int) override;
  R1 *h() override;
};
int C::g(int x) { return x; }
R1 *C::h() { return 0; }
