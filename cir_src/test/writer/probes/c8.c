struct N { struct N *next; int val; };
int g(int *p, int a[3][4], struct N *n, char c, unsigned long ul, int i, double d) {
  _Bool b = i;
  _Bool b2 = p;
  _Bool b3 = d;
  (void)i;
  i = 1[p];
  i = a[1][2];
  i = *p++;
  i = n->next->val;
  i = p < p + 1;
  p = &*p;
  i = (unsigned char)c;
  i = +i;
  n->val = c ? n->val : 0;
  switch (c) { case 'a': return 1; }
  ul = i;
  ul = (unsigned long)d;
  d = (double)ul;
  return b + b2 + b3;
}
