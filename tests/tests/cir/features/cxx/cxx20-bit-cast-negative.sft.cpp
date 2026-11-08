//type:fn
//options:--c++20
//use_system_includes: true
//require:BACK_END_TYPE 3
/* std::bit_cast (__builtin_bit_cast) is diagnosed by the CIR back end and the
   function that uses it is dropped. */
#include <bit>
unsigned float_bits(float f) { return std::bit_cast<unsigned>(f); }
