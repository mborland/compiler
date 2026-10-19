/* Two lambdas with the same signature in one inline function: their closure
   types are UlvE_ and UlvE0_ (the discriminator is ABI), and so are the names
   of the static locals of their call operators, which every translation unit
   that has the function shares. */
inline int f() {
  auto first = []() {
    static int n = 0;
    return ++n;
  };
  auto second = []() {
    static int n = 100;
    n += 10;
    return n;
  };
  return first() * 1000 + second();
}

int x();
