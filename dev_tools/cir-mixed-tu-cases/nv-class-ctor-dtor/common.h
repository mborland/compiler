// A class with out-of-line constructors, destructor and members, used from
// the other TU by value, by copy and on the heap.
struct Counter {
  static int live;
  static int made;
  int value;
  Counter();
  explicit Counter(int v);
  Counter(const Counter &other);
  ~Counter();
  int get() const;
  Counter &add(int v);
};

Counter make_counter(int v);
int sum_array(int n);
