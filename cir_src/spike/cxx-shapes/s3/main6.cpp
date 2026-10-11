#include <cstdio>
bool c6(bool,bool);
int main() { for (int i = 0; i < 4; ++i) std::printf("c6(%d,%d)=%d\n", i&1, i>>1, (int)c6(i&1, i>>1)); return 0; }
