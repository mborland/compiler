//type:rp
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <cstring>: memcpy, memmove, memset, memcmp, strlen, strcmp, strchr, strstr and friends.
#include <cstring>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  char a[16] = "hello";
  char b[16];
  std::memcpy(b, a, 6);
  CHECK(std::strlen(b) == 5 && std::strcmp(a, b) == 0);
  std::memset(b, 'x', 3);
  CHECK(b[0] == 'x' && b[2] == 'x' && b[3] == 'l');
  std::memmove(a + 1, a, 5);
  CHECK(a[0] == 'h' && a[1] == 'h' && a[5] == 'o');
  CHECK(std::strchr(a, 'e') == a + 2 && std::strrchr(a, 'h') == a + 1);
  CHECK(std::strstr("needle in haystack", "in h") != nullptr);
  CHECK(std::strncmp("abcd", "abcf", 3) == 0 && std::strcmp("a", "b") < 0);
  CHECK(std::memcmp("abc", "abd", 3) < 0 && std::memchr("abc", 'c', 3) != nullptr);
  char c[16] = "ab";
  std::strcat(c, "cd");
  CHECK(std::strlen(c) == 4 && std::strncpy(b, "zz", 2) == b);
  CHECK(std::strspn("aabbc", "ab") == 4 && std::strcspn("aabbc", "c") == 4);
  CHECK(std::strerror(2) != nullptr);
  char tok[] = "a,b;c";
  CHECK(std::strtok(tok, ",;")[0] == 'a');
  CHECK(std::strpbrk("hello", "lo") != nullptr);
  printf("cstring: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
