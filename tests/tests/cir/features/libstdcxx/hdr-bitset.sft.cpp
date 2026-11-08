//type:fn
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <bitset>: bitset bit operations, counting and conversions.
// Negative test: the CIR back end diagnoses this and drops the definition; it must fail at -O0 and -O2.
#include <bitset>
#include <string>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  std::bitset<16> b(0xF0F0);
  CHECK(b.count() == 8 && b.size() == 16);
  CHECK(b[4] && !b[0]);
  b.flip(0);
  CHECK(b[0] && b.count() == 9);
  b.reset(4);
  CHECK(!b.test(4));
  CHECK(b.to_ulong() == 0xF0E1);
  std::bitset<16> c = b & std::bitset<16>(0xFF00);
  CHECK(c.to_ulong() == 0xF000);
  CHECK(c.any() && !c.none() && !c.all());
  std::bitset<8> s(std::string("10110"));
  CHECK(s.to_ulong() == 22);
  CHECK((s << 2).to_ulong() == 88);
  CHECK(s.to_string() == "00010110");
  std::bitset<70> big;
  big.set(69);
  CHECK(big.count() == 1 && big.test(69) && !big.test(68));
  CHECK((big >> 69).to_ulong() == 1);
  printf("bitset: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
