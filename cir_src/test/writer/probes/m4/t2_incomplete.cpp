// M4a probe: typeinfo of types whose pointee chain contains an incomplete
// class (clang's ContainsIncompleteClassType): I*, I**, int I::*, const I *,
// and a class that is completed later in the TU.
#include <typeinfo>
struct I;
struct Later;
struct C { int m; };
const std::type_info *infos[] = { &typeid(I *), &typeid(I **), &typeid(int I::*), &typeid(const I *),
  &typeid(Later *), &typeid(int C::*), &typeid(I *C::*) };
struct Later { virtual ~Later() {} };       // completed after its pointer typeid
const std::type_info *after[] = { &typeid(Later *), &typeid(Later) };
int main() { return infos[0] == 0; }
