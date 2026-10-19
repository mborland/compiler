// A program that defines __debug_level itself: the archive's definition is not
// linked (no duplicate symbol), so the program's value is the one it reads.
int __debug_level = 3;

int main()
{
  return __debug_level == 3 ? 0 : 1;
}
