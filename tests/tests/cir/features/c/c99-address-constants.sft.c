//type:rp
//options:--c99:--c99 -O2
//require:BACK_END_TYPE 3
/* Address constants cast to an integer: the forms that work.  A return
   value, an assignment, an argument, a comparison and a difference of two
   addresses are translated; the initializer of a block-scope variable is
   diagnosed (c99-address-constants-negative.sft.c). */
typedef unsigned long address_t;

int g;
int array[8];
static int local_object;

static address_t of_g(void)
{
  return (address_t)&g;
}

static address_t distance(void)
{
  return (address_t)&array[5] - (address_t)array;
}

static int same(address_t value, const void *pointer)
{
  return value == (address_t)pointer;
}

int main(void)
{
  address_t x = 0;
  int failures = 0;

  x = (address_t)&g;
  if (x != of_g() || x == 0)
    failures |= 1;
  if (distance() != 5 * sizeof(int))
    failures |= 2;
  if (!same((address_t)&local_object, &local_object))
    failures |= 4;
  if ((address_t)&array[1] <= (address_t)&array[0])
    failures |= 8;
  if (((address_t)&array[2] & (sizeof(int) - 1)) != 0)
    failures |= 16;
  return failures;
}
