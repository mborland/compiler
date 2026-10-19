// Kernel: sieve of Eratosthenes with byte flags and a popcount-style tally.
#include <cstdio>
#include <vector>

int main() {
  const int n = 6000000;
  std::vector<unsigned char> composite(n + 1, 0);
  for (long i = 2; i * i <= n; ++i)
    if (!composite[i])
      for (long j = i * i; j <= n; j += i) composite[j] = 1;
  long primes = 0, bits = 0;
  for (int i = 2; i <= n; ++i)
    if (!composite[i]) {
      ++primes;
      bits += __builtin_popcount(i);
    }
  std::printf("%ld %ld\n", primes, bits);
  return 0;
}
