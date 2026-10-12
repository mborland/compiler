#include "common.h"
#include <cxxabi.h>

struct I;

void throw_i(void *p) {
  void *exception = abi::__cxa_allocate_exception(sizeof(I *));
  *static_cast<I **>(exception) = static_cast<I *>(p);
  abi::__cxa_throw(exception, const_cast<std::type_info *>(&typeid(I *)),
                   nullptr);
}

const std::type_info &typeid_of_i_pointer() { return typeid(I *); }
