// Real-world TU: iostream, sstream and formatting.
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

int main() {
  std::ostringstream out;
  out << std::setw(8) << std::setfill('0') << 42 << ' ' << std::hex << 255
      << ' ' << std::fixed << std::setprecision(3) << 3.14159 << '\n';
  std::istringstream in("12 3.5 word");
  int i; double d; std::string w;
  in >> i >> d >> w;
  std::cout << out.str() << i << d << w << std::endl;
  return 0;
}
