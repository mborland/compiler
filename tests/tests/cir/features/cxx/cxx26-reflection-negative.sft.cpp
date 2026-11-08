//type:fn
//options:--c++26 --set_flag=reflection
//require:BACK_END_TYPE 3
/* A variable of a reflection type (std::meta::info) is diagnosed by the CIR
   back end as an unsupported constant and the function is dropped. */
struct S { int a; };
int use()
{
  constexpr auto r = ^^S::a;
  S s = {3};
  return s.[:r:];
}
