//type:fn
//options:--c++23
//use_system_includes: true
//require:BACK_END_TYPE 3
/* A _Float16 constant is diagnosed by the CIR back end ("a floating constant
   of this format") and the function that uses it is dropped.  Arithmetic on
   run-time values works (the probe). */
float half_value()
{
  _Float16 x = (_Float16)1.5f;
  return (float)x;
}
