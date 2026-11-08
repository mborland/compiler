//type:rp
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <cstdio>: snprintf, sscanf, tmpfile reads and writes, fputs and fgets.
#include <cstdio>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  char buf[64];
  int n = std::snprintf(buf, sizeof buf, "%d-%s-%.2f-%x", 42, "ab", 1.5, 255);
  CHECK(n == 13 && buf[0] == '4' && buf[3] == 'a' && buf[12] == 'f');
  int a = 0; char w[16] = {};
  CHECK(std::sscanf("12 hello", "%d %15s", &a, w) == 2 && a == 12 && w[0] == 'h');
  std::FILE *f = std::tmpfile();
  CHECK(f != nullptr);
  std::fputs("line one\nline two\n", f);
  std::fprintf(f, "%d\n", 77);
  std::rewind(f);
  char l[32];
  CHECK(std::fgets(l, sizeof l, f) != nullptr && l[5] == 'o');
  CHECK(std::fgets(l, sizeof l, f) != nullptr && l[5] == 't');
  int v = 0;
  CHECK(std::fscanf(f, "%d", &v) == 1 && v == 77);
  CHECK(std::fgets(l, sizeof l, f) != nullptr);
  CHECK(std::fgets(l, sizeof l, f) == nullptr && std::feof(f));
  CHECK(std::ftell(f) == 21);
  std::fseek(f, 5, SEEK_SET);
  CHECK(std::fgetc(f) == 'o');
  std::fclose(f);
  CHECK(std::snprintf(nullptr, 0, "%05d", 7) == 5);
  std::puts("cstdio puts");
  printf("cstdio: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
