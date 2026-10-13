// Host of cxx-s4-new-array-size-throw.test.
#include <new>
extern "C" unsigned long c_size(int n);
extern "C" unsigned long c_size8(unsigned long n);
extern "C" unsigned long c_size_nothrow(int n);
int main() {
  if (c_size(3) != 12) return 1;
  if (c_size8(5) != 40) return 2;
  try { c_size(-1); return 3; } catch (std::bad_array_new_length &) {}
  try { c_size8(1ul << 62); return 4; } catch (std::bad_array_new_length &) {}
  if (c_size_nothrow(-1) != ~0ul) return 5;
  return 0;
}
