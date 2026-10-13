#include "common.h"
#include <cstdio>

iostream::iostream() { std::printf("iostream()\n"); }
iostream::~iostream() { std::printf("~iostream\n"); }
ofstream::ofstream() { std::printf("ofstream()\n"); }
ostream &ofstream::put(int c) { return ostream::put(c + fd); }
ofstream::~ofstream() { std::printf("~ofstream\n"); }
iostream *make_iostream() { return new iostream; }
ofstream *make_ofstream() { return new ofstream; }
