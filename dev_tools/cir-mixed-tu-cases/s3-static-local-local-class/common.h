/* A local class of an inline function has a member function with a static
   local: both have vague linkage (linkonce_odr, with the comdat of the
   enclosing function) and the counter is one object in the whole program,
   also when -O2 inlines the function into each translation unit. */
inline int f(int k) {
  struct L {
    int bump() {
      static int n = 0;
      return n++;
    }
  };
  L l;
  l.bump();
  return l.bump() + k;
}

int x(int);
