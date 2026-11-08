//type:fn
//options:--c++20
//use_system_includes: true
//require:BACK_END_TYPE 3
/* std::source_location::current (__builtin_source_location) as a default
   argument: the address of the source_location object is diagnosed by the CIR
   back end ("the address of a non-string constant") and the caller is
   dropped. */
#include <source_location>
const char *where(std::source_location l = std::source_location::current())
{
  return l.file_name();
}
const char *caller() { return where(); }
