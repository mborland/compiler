//type:rp
//options:--gcc:--gcc -O2
//require:BACK_END_TYPE 3
/* Asm labels on declarations: the symbol name is the label, so a second
   declaration that uses the label as its own name (through another asm
   label) reaches the same object.  glibc's redirects (stat -> stat64 and the
   like) rely on this. */
int counter_alias(void) __asm__("real_counter");
extern volatile int total_alias __asm__("real_total");
int bump_alias(int) __asm__("real_bump");

volatile int real_total = 40;

int counter_alias(void)
{
  return ++real_total;
}

int bump_alias(int v)
{
  return v + 100;
}

/* The label names the symbol that already exists. */
extern int via_label(int) __asm__("real_bump");
extern volatile int total_via_label __asm__("real_total");
extern int counter_via_label(void) __asm__("real_counter");

/* A static function with a label keeps its own file-local name. */
static int local_fn(int v) __asm__("local_label_name");
static int local_fn(int v)
{
  return v * 2;
}

int main(void)
{
  int failures = 0;

  if (counter_alias() != 41 || counter_via_label() != 42)
    failures |= 1;
  if (total_alias != 42 || total_via_label != 42)
    failures |= 2;
  total_via_label = 10;
  if (total_alias != 10 || counter_alias() != 11)
    failures |= 4;
  if (via_label(1) != 101 || bump_alias(2) != 102)
    failures |= 8;
  if (local_fn(21) != 42)
    failures |= 16;
  return failures;
}
