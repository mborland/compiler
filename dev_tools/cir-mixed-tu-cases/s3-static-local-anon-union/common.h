/* A static local of an inline function whose type is an anonymous union (the
   shape of gcc's pr39188-1).  The variable must be one object in the whole
   program (linkonce_odr, in the comdat of the function), also when -O2 inlines
   the function into each translation unit. */
inline int f(int x) {
  static union {
    int i;
  };
  int j = i;
  i = x;
  return j;
}

int x(int);
