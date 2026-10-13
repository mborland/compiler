#include "common.h"

clone_base const *make_a(int c) {
  my_error e(c);
  return new clone_impl<my_error>(e);
}
