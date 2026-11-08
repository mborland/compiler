//type:fn
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <sstream>: stringstream, istringstream, ostringstream and stringbuf.
// Negative test: the CIR back end diagnoses this and drops the definition; it must fail at -O0 and -O2.
#include <sstream>
#include <string>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  std::stringstream ss;
  ss << 10 << ' ' << 20 << ' ' << "x";
  int a, b; std::string s;
  ss >> a >> b >> s;
  CHECK(a == 10 && b == 20 && s == "x");
  std::ostringstream os;
  os << "abc" << 123;
  CHECK(os.str() == "abc123" && os.view() == "abc123");
  os.seekp(1);
  os << 'Z';
  CHECK(os.str() == "aZc123");
  std::istringstream is("line1\nline2;rest");
  std::string l1, l2, l3;
  std::getline(is, l1);
  std::getline(is, l2, ';');
  std::getline(is, l3);
  CHECK(l1 == "line1" && l2 == "line2" && l3 == "rest");
  std::stringbuf sb("hello");
  CHECK(sb.str() == "hello" && sb.sgetc() == 'h' && sb.in_avail() == 5);
  sb.str("xy");
  CHECK(sb.sbumpc() == 'x');
  std::istringstream ws("1 2 3 4");
  int tot = 0, x;
  while (ws >> x) tot += x;
  CHECK(tot == 10 && ws.eof());
  std::stringstream rw;
  rw.str("12 34");
  rw >> a;
  rw.clear();
  rw.seekg(0);
  rw >> b;
  CHECK(a == 12 && b == 12);
  CHECK(std::istringstream("5").rdbuf()->in_avail() == 1);
  printf("sstream: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
