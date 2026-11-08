//type:fn
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <locale>: locale, facets, ctype, numpunct, collate and imbue.
// Negative test: the CIR back end diagnoses this and drops the definition; it must fail at -O0 and -O2.
#include <locale>
#include <sstream>
#include <string>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  std::locale loc = std::locale::classic();
  CHECK(loc.name() == "C");
  const auto &ct = std::use_facet<std::ctype<char>>(loc);
  CHECK(ct.is(std::ctype_base::alpha, 'a') && ct.toupper('a') == 'A');
  CHECK(std::isalpha('x', loc) && std::isdigit('7', loc) && std::toupper('b', loc) == 'B');
  CHECK(std::has_facet<std::numpunct<char>>(loc));
  const auto &np = std::use_facet<std::numpunct<char>>(loc);
  CHECK(np.decimal_point() == '.' && np.thousands_sep() == ',' && np.truename() == "true");
  const auto &co = std::use_facet<std::collate<char>>(loc);
  CHECK(co.compare("abc", "abc" + 3, "abd", "abd" + 3) < 0);
  std::ostringstream os;
  os.imbue(std::locale::classic());
  os << 1234567;
  CHECK(os.str() == "1234567" && os.getloc() == std::locale::classic());
  std::locale l2(std::locale::classic(), new std::numpunct<char>());
  CHECK(l2.name() == "*" || l2.name() == "C");
  std::locale c("C");
  CHECK(c == loc);
  std::locale g = std::locale::global(loc);
  CHECK(g.name() == "C");
  CHECK(std::use_facet<std::ctype<wchar_t>>(loc).is(std::ctype_base::digit, L'4'));
  printf("locale: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
