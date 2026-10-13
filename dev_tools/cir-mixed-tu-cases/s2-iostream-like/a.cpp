#include "common.h"
#include <cstdio>

ios_base::~ios_base() { std::printf("~ios_base\n"); }
ios::ios() { std::printf("ios()\n"); }
void ios::init(int mode) { state = mode; std::printf("ios::init %d\n", mode); }
ios::~ios() { std::printf("~ios\n"); }
ostream::ostream() { init(1); std::printf("ostream()\n"); }
ostream &ostream::put(int c) { ++count; state += c; return *this; }
ostream &ostream::flush() {
  std::printf("flush %ld %d\n", count, state);
  return *this;
}
ostream::~ostream() { std::printf("~ostream\n"); }
istream::istream() { init(2); std::printf("istream()\n"); }
int istream::get() { ++gcount; return fill + state; }
istream::~istream() { std::printf("~istream\n"); }
void destroy_ios(ios_base *p) { delete p; }
