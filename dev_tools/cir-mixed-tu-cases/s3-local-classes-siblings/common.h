/* Local classes with the same name in sibling blocks of one inline function:
   the second is L_0 in the mangling (the discriminator is ABI), and the
   static locals of their member functions are shared between translation
   units. */
inline int f(int k) {
  int sum = 0;
  {
    struct L {
      int bump(int d) {
        static int n = 0;
        n += d;
        return n;
      }
    };
    L l;
    sum += l.bump(k);
  }
  {
    struct L {
      int bump(int d) {
        static int n = 1000;
        n += d;
        return n;
      }
    };
    L l;
    sum += l.bump(k);
  }
  return sum;
}

int x(int);
