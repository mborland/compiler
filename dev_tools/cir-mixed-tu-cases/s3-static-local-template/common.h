/* As s3-static-local-anon-union, for the instance of a function template
   (gcc's pr39188-2): the static local is one object in the whole program. */
template <class T>
inline T f(T x) {
  static union {
    T i;
  };
  T j = i;
  i = x;
  return j;
}

int x(int);
