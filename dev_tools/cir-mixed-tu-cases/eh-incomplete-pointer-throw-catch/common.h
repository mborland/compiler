/* A pointer to a class that is incomplete in the throwing translation unit
   and complete in the catching one.  Throwing a pointer to an incomplete
   class is ill-formed C++, so a.cpp throws through the ABI entry points: its
   typeinfo for I* is then the one that the compiler makes for an incomplete
   I (the "incomplete" flag of __pointer_type_info), and the handler
   catch (I *) in b.cpp, where I is complete, must still match it. */
#include <typeinfo>

/* a.cpp: throws (I *)p */
void throw_i(void *p);
/* a.cpp: typeid(I *), I incomplete */
const std::type_info &typeid_of_i_pointer();
