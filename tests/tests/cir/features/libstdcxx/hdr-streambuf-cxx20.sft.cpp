//type:rp
//options:--c++20 --diag_suppress=cir_warning:--c++20 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <streambuf>: a user streambuf that counts and records output, and a get area.
#include <streambuf>
#include <istream>
#include <ostream>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
struct Counting : std::streambuf {
  int n = 0;
  char last = 0;
  int_type overflow(int_type c) override {
    if (c != traits_type::eof()) { ++n; last = char(c); }
    return c;
  }
};
struct Source : std::streambuf {
  char data[16];
  Source(const char *s) {
    int i = 0;
    while (s[i]) { data[i] = s[i]; ++i; }
    setg(data, data, data + i);
  }
};
int main() {
  Counting cb;
  std::ostream os(&cb);
  os << "hello" << 42 << 'x';
  os.flush();
  CHECK(cb.n == 8 && cb.last == 'x');
  CHECK(os.good());
  Source src("abcdef");
  std::istream is(&src);
  char buf[8] = {};
  is.read(buf, 4);
  CHECK(is.gcount() == 4 && buf[0] == 'a' && buf[3] == 'd');
  CHECK(src.sgetc() == 'e' && src.sbumpc() == 'e' && src.sgetc() == 'f');
  CHECK(src.sungetc() == 'e');
  char b2[4] = {};
  CHECK(src.sgetn(b2, 4) == 2 && b2[0] == 'e' && b2[1] == 'f');
  CHECK(src.sgetc() == std::char_traits<char>::eof());
  CHECK(cb.sputc('q') == 'q' && cb.n == 9);
  CHECK(cb.sputn("abc", 3) == 3 && cb.n == 12);
  printf("streambuf: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
