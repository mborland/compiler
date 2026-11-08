//type:rp
//options:--c23:--c23 -O2
//use_system_includes: true
//require:BACK_END_TYPE 3
/* IEEE 754 interchange and extended types (_Float16, _Float32, _Float64,
   _Float128, _Float32x, _Float64x): arithmetic and conversions of values
   that the program computes work.  A floating constant of the formats that
   are not float, double or long double is the part that is diagnosed (see
   c23-float-types-negative.sft.c): every value here comes from an int. */
_Float16 add16(_Float16 a, _Float16 b) { return a + b; }
_Float32 add32(_Float32 a, _Float32 b) { return a + b; }
_Float64 add64(_Float64 a, _Float64 b) { return a + b; }
_Float128 mul128(_Float128 a, _Float128 b) { return a * b; }
_Float32x addx32(_Float32x a, _Float32x b) { return a + b; }
_Float64x addx64(_Float64x a, _Float64x b) { return a + b; }

int main(void)
{
  volatile int one = 1;
  volatile int two = 2;
  volatile int zero = 0;
  _Float16 half = (_Float16)one / (_Float16)two;
  _Float128 big = (_Float128)two;
  int failures = 0;

  if ((int)add16((_Float16)one, (_Float16)one) != 2)
    failures |= 1;
  if ((int)mul128(big, big) != 4 || (int)mul128(big, mul128(big, big)) != 8)
    failures |= 2;
  if ((int)add32(one, two) != 3 || (int)add64(one, two) != 3)
    failures |= 4;
  if ((int)addx32(one, two) != 3 || (int)addx64(one, two) != 3)
    failures |= 8;
  if ((int)(half * (_Float16)(two * two)) != 2 || sizeof(_Float16) != 2
      || sizeof(_Float128) != 16)
    failures |= 16;
  if (!(big > (_Float128)one && half < (_Float16)one && half > (_Float16)zero))
    failures |= 32;
  return failures;
}
