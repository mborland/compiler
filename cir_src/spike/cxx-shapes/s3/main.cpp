#include <cstdio>
int c1(bool); bool c2(bool); bool c3(bool,bool); int c4(bool);
int main() {
  std::printf("c1(1)=%d\n", c1(true)); std::printf("c1(0)=%d\n", c1(false));
  std::printf("c2(1)=%d\n", (int)c2(true)); std::printf("c2(0)=%d\n", (int)c2(false));
  std::printf("c3(1,1)=%d\n", (int)c3(true,true)); std::printf("c3(0,1)=%d\n", (int)c3(false,true));
  std::printf("c4(1)=%d\n", c4(true)); std::printf("c4(0)=%d\n", c4(false));
  return 0;
}
