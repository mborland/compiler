// Sets and reads EDG's runtime variable, as lib tests such as rma/lifetime18
// do: the front end's own tests declare it extern int.
extern "C" void exit(int);
extern int __debug_level;

int main()
{
  if (__debug_level != 0)
    exit(1);                // the default is 0
  __debug_level = 5;
  if (__debug_level != 5)
    exit(2);
  exit(0);
}
