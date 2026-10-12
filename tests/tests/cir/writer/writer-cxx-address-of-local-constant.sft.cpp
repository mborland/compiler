//type:fp
//options:-w -tused --c++26
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// A reference bound to a base class subobject of an automatic local is an
// address constant that names a local variable (an automatic one has a local
// id, not a G-id): the function is unsupported instead of an assertion
// failure in ecil_check_variable_use (edg/cmeerw/gnats-26823).

template<int I, typename ... Ts>
struct C : Ts ... [I]
{ };

struct B1 { };
struct B2 { };

struct D1 : B1, B2 { };

inline void f()
{
  C<0, B1, B2> c0;
  B1 &b1 = c0;

  C<1, B1, B2> c1;
  B2 &b2 = c1;
}

inline void g()
{
  D1 d;
  B2 &b2 = d;
}
