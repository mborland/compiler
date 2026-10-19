// Kernel: dense double matrix multiply.
#include <cstdio>

static double a[160][160], b[160][160], c[160][160];

int main() {
  for (int i = 0; i < 160; ++i)
    for (int j = 0; j < 160; ++j) {
      a[i][j] = (i * 31 + j * 17) % 23 * 0.5;
      b[i][j] = (i * 13 + j * 7) % 19 * 0.25;
    }
  for (int i = 0; i < 160; ++i)
    for (int k = 0; k < 160; ++k)
      for (int j = 0; j < 160; ++j)
        c[i][j] += a[i][k] * b[k][j];
  double sum = 0;
  for (int i = 0; i < 160; ++i)
    for (int j = 0; j < 160; ++j) sum += c[i][j];
  std::printf("%.1f\n", sum);
  return 0;
}
