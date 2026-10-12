// A class template explicitly instantiated in a.cpp and declared extern here,
// function templates and inline functions instantiated in both TUs.
template <class T> struct Box {
  T v;
  explicit Box(T x) : v(x) {}
  T get() const { return v; }
  template <class U> U cast_add(U u) const { return static_cast<U>(v) + u; }
  static int instances;
};

template <class T> int Box<T>::instances = 0;

template <class T> T twice(T x) { return x + x; }

extern template struct Box<int>;

inline long inline_sum(long a, long b) {
  static long calls = 0;
  ++calls;
  return a + b + calls;
}

int a_work();
long a_inline(long x);
