// Kernel: std::string building, searching and replacing.
#include <cstdio>
#include <string>

int main() {
  std::string s;
  for (int i = 0; i < 20000; ++i) {
    s += "item";
    s += std::to_string(i);
    s += ';';
  }
  std::size_t count = 0, pos = 0;
  while ((pos = s.find("item1", pos)) != std::string::npos) {
    ++count;
    s.replace(pos, 5, "x1");
    ++pos;
  }
  std::printf("%zu %zu\n", count, s.size());
  return 0;
}
