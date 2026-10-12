#include "common.h"

namespace util {
int counter = 5;
int bump(int by) { counter += by; return counter; }
int bump(long by) { counter += static_cast<int>(by) * 2; return counter; }
const char *name() { return "util"; }
}  // namespace util

int Registry::count = 0;
int Registry::add(int v) { count += v; return count; }

int a_next() { return next_id(); }
