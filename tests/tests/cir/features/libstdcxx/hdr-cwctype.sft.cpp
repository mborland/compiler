//type:rp
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <cwctype>: wide character classification and conversion.
#include <cwctype>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  CHECK(std::iswalpha(L'a') && !std::iswalpha(L'1') && std::iswdigit(L'7'));
  CHECK(std::iswupper(L'A') && std::iswlower(L'a') && std::iswspace(L' ') && std::iswpunct(L'!'));
  CHECK(std::towupper(L'q') == L'Q' && std::towlower(L'Q') == L'q');
  std::wctype_t wt = std::wctype("alpha");
  CHECK(wt != 0 && std::iswctype(L'x', wt) && !std::iswctype(L'1', wt));
  std::wctrans_t tr = std::wctrans("toupper");
  CHECK(tr != 0 && std::towctrans(L'm', tr) == L'M');
  CHECK(std::iswxdigit(L'f') && std::iswalnum(L'9') && std::iswprint(L'~') && std::iswcntrl(L'\n'));
  printf("cwctype: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
