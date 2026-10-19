/* An unnamed local struct with a member function in an inline function (the
   Ut_ mangling): the static local of the member function is shared between
   translation units. */
inline int f(int k) {
  struct {
    int bump(int d) {
      static int n = 0;
      n += d;
      return n;
    }
  } u;
  return u.bump(k);
}

int x(int);
