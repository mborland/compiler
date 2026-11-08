//type:fn
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <fstream>: ofstream, ifstream, fstream and filebuf on a scratch file.
// Negative test: the CIR back end diagnoses this and drops the definition; it must fail at -O0 and -O2.
#include <fstream>
#include <string>
#include <cstdio>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  const char *path = "hdr_fstream_scratch.txt";
  {
    std::ofstream out(path);
    CHECK(out.is_open() && out.good());
    out << "alpha 1\nbeta 2\n" << 3.5 << '\n';
  }
  {
    std::ifstream in(path);
    CHECK(in.is_open());
    std::string w; int n;
    in >> w >> n;
    CHECK(w == "alpha" && n == 1);
    in >> w >> n;
    CHECK(w == "beta" && n == 2);
    double d;
    in >> d;
    CHECK(d == 3.5);
    CHECK(in.get() == '\n');
    CHECK(in.get() == std::char_traits<char>::eof() && in.eof());
  }
  {
    std::fstream rw(path, std::ios::in | std::ios::out | std::ios::ate);
    CHECK(rw.tellg() == std::streampos(19));
    rw.seekp(0);
    rw << "ALPHA";
    rw.seekg(0);
    std::string w;
    rw >> w;
    CHECK(w == "ALPHA");
  }
  std::ifstream missing("/edg-cir-nonexistent/file");
  CHECK(!missing.is_open() && missing.fail());
  std::filebuf fb;
  CHECK(fb.open(path, std::ios::in) != nullptr && fb.is_open());
  CHECK(fb.sbumpc() == 'A');
  fb.close();
  CHECK(!fb.is_open());
  CHECK(std::remove(path) == 0);
  printf("fstream: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
