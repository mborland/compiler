// Kernel: floating point updates over an array of structs.
#include <cmath>
#include <cstdio>

struct P { double x, y, z, vx, vy, vz, m; };

int main() {
  static P p[64];
  for (int i = 0; i < 64; ++i)
    p[i] = {i * 0.5, i * 0.25, i * 0.125, 0, 0, 0, 1.0 + i % 3};
  for (int step = 0; step < 300; ++step) {
    for (int i = 0; i < 64; ++i)
      for (int j = 0; j < 64; ++j) {
        if (i == j) continue;
        double dx = p[j].x - p[i].x, dy = p[j].y - p[i].y,
               dz = p[j].z - p[i].z;
        double d2 = dx * dx + dy * dy + dz * dz + 0.01;
        double inv = p[j].m / (d2 * std::sqrt(d2));
        p[i].vx += dx * inv * 0.001;
        p[i].vy += dy * inv * 0.001;
        p[i].vz += dz * inv * 0.001;
      }
    for (int i = 0; i < 64; ++i) {
      p[i].x += p[i].vx * 0.01;
      p[i].y += p[i].vy * 0.01;
      p[i].z += p[i].vz * 0.01;
    }
  }
  double e = 0;
  for (int i = 0; i < 64; ++i) e += p[i].vx * p[i].vx + p[i].vy * p[i].vy;
  std::printf("%.6f\n", e);
  return 0;
}
