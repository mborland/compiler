// Real-world TU: <chrono> durations and arithmetic.
#include <chrono>
#include <ratio>

int main() {
  using namespace std::chrono;
  auto a = milliseconds(1500);
  auto b = duration_cast<seconds>(a);
  auto c = duration<double, std::ratio<60>>(2.5);
  auto t = system_clock::time_point{} + hours(25);
  auto d = duration_cast<minutes>(t.time_since_epoch());
  return int(b.count() + d.count() + int(c.count())) == 0;
}
