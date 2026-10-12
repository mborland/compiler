// Free functions, namespaces, globals and static members across two TUs.
namespace util {
extern int counter;
int bump(int by);
int bump(long by);
const char *name();
}  // namespace util

struct Registry {
  static int count;
  static int add(int v);
  int id;
};

inline int next_id() {
  static int n = 100;
  return ++n;
}
