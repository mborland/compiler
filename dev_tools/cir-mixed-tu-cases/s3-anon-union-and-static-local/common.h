/* A static anonymous union next to a static local of the same name in a
   nested block of one inline function: the variables are distinct objects,
   and their discriminators in the mangling are ABI. */
inline int f(int x) {
  static union {
    int i;
  };
  int j = i;
  i = x;
  {
    static int i = 100;
    i += j;
    j += i;
  }
  return j;
}

int x(int);
