// S4 probe: the standard exception classes, thrown and caught by base, with
// std::string messages, std::vector::at, rethrow and nested try; compared with
// g++. (std::string and std::vector use throw helpers of the library.)
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>
#include <exception>
struct MyErr : std::runtime_error { MyErr(const std::string &s) : std::runtime_error(s) { } };
int depth(int n) { if (n == 0) throw MyErr("bottom"); return depth(n - 1) + 1; }
int main() {
  try { depth(3); } catch (const std::exception &e) { std::printf("%s\n", e.what()); }
  try { std::vector<int> v(2); v.at(5); } catch (const std::out_of_range &e) { std::puts("out_of_range"); }
  try { throw std::logic_error("logic"); } catch (const std::runtime_error &) { std::puts("no"); } catch (const std::logic_error &e) { std::printf("%s\n", e.what()); }
  try { try { throw 1; } catch (int) { throw std::string("inner"); } } catch (const std::string &s) { std::printf("%s\n", s.c_str()); }
  try { std::string s("abc"); s.substr(10); } catch (const std::out_of_range &) { std::puts("substr"); }
  std::exception_ptr p;
  try { throw 7; } catch (...) { p = std::current_exception(); }
  try { std::rethrow_exception(p); } catch (int i) { std::printf("%d\n", i); }
  return 0;
}
