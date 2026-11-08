//type:rp
//options:--c++17 --no_rtti:--c++17 --no_rtti -O2
//require:BACK_END_TYPE 3
// RTTI off (--no_rtti): virtual dispatch, virtual destructors, multiple and
// virtual inheritance with their thunks, covariant returns, and exceptions
// (the types that a throw and a catch name still get type information) all
// work; typeid and dynamic_cast are what the option takes away.
struct Shape {
  virtual ~Shape() {}
  virtual int sides() const = 0;
  virtual Shape *self() { return this; }
  int tag = 1;
};

struct Square : Shape {
  int sides() const override { return 4; }
  Square *self() override { return this; }      // covariant
};

struct Triangle : Shape {
  int sides() const override { return 3; }
};

struct Named {
  virtual const char *name() const { return "named"; }
  virtual ~Named() {}
};

struct Both : Square, Named {
  const char *name() const override { return "both"; }
};

struct Base {
  int value = 7;
  virtual int get() const { return value; }
  virtual ~Base() {}
};
struct Left : virtual Base {
  int get() const override { return value + 1; }
};
struct Right : virtual Base {};
struct Diamond : Left, Right {
  int get() const override { return Left::get() + 10; }
};

static int destroyed;
struct Counted : Shape {
  ~Counted() override { ++destroyed; }
  int sides() const override { return 0; }
};

struct Problem {
  int code;
};

static int thrower(int v)
{
  if (v == 1)
    throw 42;
  if (v == 2)
    throw Problem{7};
  return v;
}

int main()
{
  int failures = 0;
  Square square;
  Triangle triangle;
  Shape *shapes[2] = { &square, &triangle };
  Both both;
  Named *named = &both;
  Shape *as_shape = &both;
  Diamond diamond;
  Base *base = &diamond;

  if (shapes[0]->sides() != 4 || shapes[1]->sides() != 3)
    failures |= 1;
  if (shapes[0]->self() != &square || square.self() != &square)
    failures |= 2;
  if (as_shape->sides() != 4 || named->name()[0] != 'b')
    failures |= 4;                      // through the secondary-base thunk
  if (base->get() != 18)
    failures |= 8;
  {
    Shape *counted = new Counted;
    delete counted;                     // virtual destructor
  }
  if (destroyed != 1)
    failures |= 16;
  try {
    thrower(1);
    failures |= 32;
  } catch (int caught) {
    if (caught != 42)
      failures |= 32;
  }
  try {
    thrower(2);
    failures |= 64;
  } catch (const Problem &caught) {
    if (caught.code != 7)
      failures |= 64;
  }
  if (thrower(5) != 5)
    failures |= 128;
  return failures;
}
