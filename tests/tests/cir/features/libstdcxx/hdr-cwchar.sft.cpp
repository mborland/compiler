//type:rp
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <cwchar>: wide string functions, btowc, wcstol and swprintf.
#include <cwchar>
#include <cstdio>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  wchar_t a[16] = L"hello";
  wchar_t b[16];
  std::wcscpy(b, a);
  CHECK(std::wcslen(b) == 5 && std::wcscmp(a, b) == 0);
  std::wcscat(b, L"!!");
  CHECK(std::wcslen(b) == 7 && std::wcscmp(a, b) < 0);
  std::wmemset(b, L'x', 3);
  CHECK(b[2] == L'x' && b[3] == L'l');
  CHECK(std::wcschr(a, L'l') == a + 2 && std::wcsstr(a, L"llo") == a + 2);
  CHECK(std::wcstol(L"-42z", nullptr, 10) == -42 && std::wcstod(L"1.5", nullptr) == 1.5);
  CHECK(std::btowc('a') == L'a' && std::wctob(L'b') == 'b');
  wchar_t out[32];
  CHECK(std::swprintf(out, 32, L"%d-%ls", 5, L"ab") == 4 && out[0] == L'5');
  std::wmemcpy(out, a, 6);
  CHECK(std::wmemcmp(out, a, 6) == 0 && std::wcsncmp(out, a, 3) == 0);
  std::mbstate_t st{};
  wchar_t w = 0;
  CHECK(std::mbrtowc(&w, "x", 1, &st) == 1 && w == L'x');
  char mb[8];
  CHECK(std::wcrtomb(mb, L'y', &st) == 1 && mb[0] == 'y');
  printf("cwchar: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
