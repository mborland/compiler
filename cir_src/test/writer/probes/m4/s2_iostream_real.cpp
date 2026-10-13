//options:--c++17
// M4 slice S2: the real iostream (basic_ostream : virtual basic_ios).  Needs
// exceptions in the library code (S4/S5): until then the functions that contain
// try/throw are dropped and this probe fails to link or run.
#include <iostream>
int main() {
  std::cout << "hello " << 42 << ' ' << 1.5 << "\n";
  return 0;
}
