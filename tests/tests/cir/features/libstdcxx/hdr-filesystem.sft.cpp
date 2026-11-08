//type:fn
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <filesystem>: path manipulation, status queries and directory iteration.
// Negative test: the CIR back end diagnoses this and drops the definition; it must fail at -O0 and -O2.
#include <filesystem>
#include <fstream>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  namespace fs = std::filesystem;
  fs::path p = "/usr/local/lib/libfoo.so.1";
  CHECK(p.filename() == "libfoo.so.1" && p.extension() == ".1" && p.stem() == "libfoo.so");
  CHECK(p.parent_path() == "/usr/local/lib" && p.root_path() == "/" && p.is_absolute());
  fs::path q = fs::path("a") / "b" / "c.txt";
  CHECK(q == "a/b/c.txt" && q.relative_path() == q && q.is_relative());
  q.replace_extension(".md");
  CHECK(q.string() == "a/b/c.md");
  CHECK(fs::path("a/./b/../c").lexically_normal() == "a/c");
  CHECK(fs::path("/a/b/c").lexically_relative("/a") == "b/c");
  int parts = 0;
  for (auto &e : p) { (void)e; ++parts; }
  CHECK(parts == 5);
  std::error_code ec;
  CHECK(fs::exists("/", ec) && fs::is_directory("/", ec));
  CHECK(!fs::exists("/edg-cir-nonexistent-path", ec));
  auto tmp = fs::temp_directory_path(ec);
  CHECK(!tmp.empty());
  fs::path d = "hdr_filesystem_scratch";
  fs::remove_all(d, ec);
  CHECK(fs::create_directories(d / "sub", ec));
  { std::ofstream(d / "sub" / "f.txt") << "12345"; }
  CHECK(fs::file_size(d / "sub" / "f.txt") == 5 && fs::is_regular_file(d / "sub" / "f.txt"));
  int n = 0;
  for (auto &e : fs::recursive_directory_iterator(d)) { (void)e; ++n; }
  CHECK(n == 2);
  CHECK(fs::remove_all(d, ec) == 3);
  CHECK(!fs::current_path().empty());
  printf("filesystem: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
