/* A lambda of an inline function has a static local: the call operator and
   the variable have vague linkage and the variable is one object in the whole
   program, also when -O2 inlines the function into each translation unit. */
inline int g(int k) {
  auto l = [](int d) {
    static int n = 0;
    n += d;
    return n;
  };
  return l(k);
}

int x(int);
