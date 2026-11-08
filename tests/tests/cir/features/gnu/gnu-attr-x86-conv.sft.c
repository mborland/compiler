//type:rp
//options:--gcc:--gcc -O2
//require:BACK_END_TYPE 3
/* regparm, stdcall, fastcall and thiscall are 32-bit x86 conventions: gcc and
   clang ignore them on x86-64 (with a warning), so these functions are called
   with the ordinary System V convention and the program behaves the same. */
int __attribute__((regparm(3))) reg3(int a, int b, int c)
{
  return a * 100 + b * 10 + c;
}

int __attribute__((stdcall)) std2(int a, int b)
{
  return a - b;
}

int __attribute__((fastcall)) fast3(int a, int b, int c)
{
  return a + b + c;
}

int __attribute__((thiscall)) this2(int a, int b)
{
  return a * b;
}

typedef int __attribute__((stdcall)) (*std_pointer)(int, int);

int main(void)
{
  volatile std_pointer pointer = std2;
  int failures = 0;

  if (reg3(1, 2, 3) != 123)
    failures |= 1;
  if (std2(9, 4) != 5 || pointer(9, 4) != 5)
    failures |= 2;
  if (fast3(1, 2, 3) != 6)
    failures |= 4;
  if (this2(6, 7) != 42)
    failures |= 8;
  return failures;
}
