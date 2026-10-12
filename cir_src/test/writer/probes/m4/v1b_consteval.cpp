//options: --c++20
// M4a probe: a consteval virtual function has no usable number.virtual_function
// (il_def.h:13115, "undefined when is_consteval").  Does the FE give it a slot?
struct A { virtual consteval int f() const { return 1; } virtual int g() const { return 2; } virtual int h() const { return 5; } };
struct B : A { consteval int f() const override { return 3; } int g() const override { return 4; } int h() const override { return 6; } };
constexpr B gb;
constexpr const A &ga = gb;
static_assert(ga.f() == 3);
int main() { B b; const A &a = b; return a.g() - 4 + a.h() - 6; }
