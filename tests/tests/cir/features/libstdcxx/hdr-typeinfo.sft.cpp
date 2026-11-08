//type:rp
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <typeinfo>: typeid, type_info comparison, bad_cast and bad_typeid.
#include <typeinfo>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
struct Base { virtual ~Base() {} };
struct Der : Base {};
int main() {
  CHECK(typeid(int) == typeid(int) && typeid(int) != typeid(long));
  Der d;
  Base &b = d;
  CHECK(typeid(b) == typeid(Der) && typeid(b) != typeid(Base));
  CHECK(typeid(int).hash_code() == typeid(int).hash_code());
  CHECK(typeid(int).before(typeid(long)) != typeid(long).before(typeid(int)));
  CHECK(typeid(Der).name() != nullptr);
  bool caught = false;
  try { Base bb; (void)dynamic_cast<Der &>(bb); } catch (const std::bad_cast &) { caught = true; }
  CHECK(caught);
  caught = false;
  Base *np = nullptr;
  try { (void)typeid(*np); } catch (const std::bad_typeid &) { caught = true; }
  CHECK(caught);
  CHECK(typeid(const int) == typeid(int));
  printf("typeinfo: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
