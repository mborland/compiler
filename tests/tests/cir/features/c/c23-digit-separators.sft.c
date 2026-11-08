//type:rp
//options:--c23:--c23 -O2
//require:BACK_END_TYPE 3
/* Digit separators (C23) in decimal, hexadecimal, octal, binary (C23) and
   floating constants, and a char constant. */
int main(void)
{
  int failures = 0;

  if (1'000'000 != 1000000)
    failures |= 1;
  if (0xFF'FF != 65535 || 0x1'0000 != 65536)
    failures |= 2;
  if (0'17 != 15 || 0b1010'1010 != 170)
    failures |= 4;
  if (1'0.5'0 != 10.5 || 1'0e1'0 != 10e10)
    failures |= 8;
  if (123'456'789'012LL != 123456789012LL || 1'0u != 10u)
    failures |= 16;
  return failures;
}
