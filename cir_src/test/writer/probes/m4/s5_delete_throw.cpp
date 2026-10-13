//options: --c++11
extern "C" int printf(const char *, ...);
static int deleted;

void operator delete (void *) noexcept
{
  deleted = 1;
}

struct Foo {
  ~Foo()
  noexcept(false)
  {throw 1;}
};

struct Baz {
  void operator delete (void *) noexcept
  {
    deleted = 2;
  }
  virtual ~Baz()
  noexcept(false)
  {throw 1;}
};

int non_virt ()
{
  deleted = 0;
  
  Foo *p = new Foo;
  try { delete p; }
  catch (...) { return deleted != 1;}
  return 1;
}

int virt_glob ()
{
  deleted = 0;
  
  Baz *p = ::new Baz;
  try { ::delete p; }
  catch (...) { return deleted != 1;}
  return 1;
}

int virt_del ()
{
  deleted = 0;
  
  Baz *p = new Baz;
  try { delete p; }
  catch (...) { return deleted != 2;}
  return 1;
}

int ary ()
{
  deleted = 0;

  Baz *p = new Baz[1];
  try { delete[] p; }
  catch (...) { return deleted != 1;}
  return 1;
}

int main_old ()
{
  if (non_virt ())
    return 1;

  if (virt_glob ())
    return 2;

  if (virt_del ())
    return 3;

  if (ary ())
    return 4;
  
  return 0;
}

int main(int argc, char **) {
  if (argc == 1) {
    printf("non_virt %d\n", non_virt());
    printf("virt_glob %d\n", virt_glob());
    printf("virt_del %d\n", virt_del());
    printf("ary %d\n", ary());
  }
  return 0;
}
