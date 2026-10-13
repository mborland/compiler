#include "common.h"
#include <cstdio>

int main() {
  iostream *io = make_iostream();
  ostream *o = io;
  istream *i = io;
  o->put(5).put(6).flush();
  std::printf("get %d %d\n", i->get(), i->get());
  destroy_ios(io);
  ofstream *f = make_ofstream();
  ostream *fo = f;
  fo->put(1).flush();
  destroy_ios(f);
  return 0;
}
