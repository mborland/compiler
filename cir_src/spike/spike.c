/* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception */
int g = 5;
int f(int a, int b) { return a < b ? a + b : 0; }
int main(void) {
  int s = 0;
  for (int i = 0; i < g; ++i) s += f(i, 3);
  return s == 12 ? 0 : 1;
}
