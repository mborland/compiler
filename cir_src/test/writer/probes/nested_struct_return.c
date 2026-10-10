#include <stdio.h>
struct slice { unsigned char *data; long len; long cap; };
struct ret { long advance; struct slice token; };
__attribute__((noinline)) struct ret f(struct slice data, long i) {
  long advance;
  struct slice token;
  int r = 0;
  if (i > 0) {
    struct ret r;
    advance = i + 5;
    token.data = data.data + i;
    token.len = i;
    token.cap = data.cap - i;
    r.advance = advance;
    r.token = token;
    return r;
  }
  {
    struct ret r;
    advance = 0; token.data = 0; token.len = 0; token.cap = 0;
    r.advance = advance; r.token = token;
    return r;
  }
}
int main(void) {
  unsigned char buf[10] = " a ";
  struct slice s = { buf, 3, 10 };
  struct ret r = f(s, 2); printf("%ld %ld %ld %d\n", r.advance, r.token.len, r.token.cap, r.token.data[0]);
  r = f(s, 0); printf("%ld %ld %ld\n", r.advance, r.token.len, r.token.cap);
  return 0;
}
