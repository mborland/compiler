//options:--c++17
// Tuple-like structured bindings (get<N>) in a range-based for, and
// bindings of arrays of pairs.
extern "C" int printf(const char *, ...);
namespace std {
template<class T> struct tuple_size;
template<unsigned long N, class T> struct tuple_element;
template<class A, class B> struct pair {
  A first; B second;
};
template<class A, class B> struct tuple_size<pair<A, B>> {
  static const unsigned long value = 2;
};
template<class A, class B> struct tuple_element<0, pair<A, B>> { typedef A type; };
template<class A, class B> struct tuple_element<1, pair<A, B>> { typedef B type; };
template<unsigned long N, class A, class B> struct pick;
template<class A, class B> struct pick<0, A, B> {
  static A &get(pair<A, B> &p) { return p.first; }
  static const A &get(const pair<A, B> &p) { return p.first; }
  static A &&get(pair<A, B> &&p) { return static_cast<A &&>(p.first); }
};
template<class A, class B> struct pick<1, A, B> {
  static B &get(pair<A, B> &p) { return p.second; }
  static const B &get(const pair<A, B> &p) { return p.second; }
  static B &&get(pair<A, B> &&p) { return static_cast<B &&>(p.second); }
};
template<unsigned long N, class A, class B>
auto &get(pair<A, B> &p) { return pick<N, A, B>::get(p); }
template<unsigned long N, class A, class B>
const auto &get(const pair<A, B> &p) { return pick<N, A, B>::get(p); }
template<unsigned long N, class A, class B>
auto &&get(pair<A, B> &&p) { return pick<N, A, B>::get(static_cast<pair<A, B> &&>(p)); }
}
struct Map {
  std::pair<int, int> e[3];
  std::pair<int, int> *begin() { return e; }
  std::pair<int, int> *end() { return e + 3; }
};
int main() {
  Map m = { { { 1, 2 }, { 3, 4 }, { 5, 6 } } };
  int s = 0;
  for (auto &[k, v] : m) s += k + v;
  printf("%d\n", s);
  for (auto &[k, v] : m) { k += 10; v *= 2; }
  for (const auto &[k, v] : m) printf("%d %d\n", k, v);
  std::pair<int, int> arr[2] = { { 7, 8 }, { 9, 10 } };
  for (auto [k, v] : arr) printf("%d %d\n", k, v);
  for (auto &[k, v] : arr) k = v;
  printf("%d %d\n", arr[0].first, arr[1].first);
  return 0;
}
