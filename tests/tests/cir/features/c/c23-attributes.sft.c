//type:rp
//options:--c23:--c23 -O2
//require:BACK_END_TYPE 3
/* Standard attributes (C23): [[deprecated]], [[nodiscard]], [[maybe_unused]],
   [[fallthrough]], [[noreturn]], and the __has_c_attribute test.  The
   deprecated entities are used on purpose (a warning is recorded). */
#if !__has_c_attribute(deprecated) || !__has_c_attribute(nodiscard) \
    || !__has_c_attribute(maybe_unused) || !__has_c_attribute(fallthrough) \
    || !__has_c_attribute(noreturn)
#error "a standard attribute is not recognized"
#endif

[[deprecated("old")]] static int old_value(void) { return 3; }
[[nodiscard]] static int checked(int v) { return v + 1; }
[[noreturn]] static void stop(void) { for (;;) ; }
struct [[deprecated]] old_type { int v; };
enum [[maybe_unused]] color { red, green };

static int classify(int v)
{
  int result = 0;

  switch (v) {
  case 1:
    result += 1;
    [[fallthrough]];
  case 2:
    result += 10;
    break;
  default:
    result = -1;
  }
  return result;
}

int main(void)
{
  [[maybe_unused]] int unused = 5;
  struct old_type o = { 9 };
  int failures = 0;

  if (old_value() != 3)
    failures |= 1;
  if (checked(1) != 2)
    failures |= 2;
  if (classify(1) != 11 || classify(2) != 10 || classify(3) != -1)
    failures |= 4;
  if (o.v != 9)
    failures |= 8;
  if (failures == 12345)
    stop();
  return failures;
}
