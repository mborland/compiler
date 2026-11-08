//type:rp
//options:--gcc:--gcc -O2
//use_system_includes: true
//require:BACK_END_TYPE 3
/* The GNU __thread specifier in C: a thread has its own copy of the variable,
   an extern declaration is thread-local too, and a block-scope static
   variable is per thread. */
#include <pthread.h>
#include <stdio.h>

__thread int counter = 10;
extern __thread int later;
__thread int later = 5;

static int seen[3];

static int bump(void)
{
  static __thread int calls = 100;

  return ++calls;
}

static void *worker(void *unused)
{
  (void)unused;
  seen[0] = counter;            /* a fresh 10, not main's 99 */
  seen[1] = later + bump();     /* 5 + 101 */
  counter += 1;
  later += 1;
  return 0;
}

int main(void)
{
  pthread_t thread;
  int failures = 0;

  counter = 99;
  later = 6;
  bump();
  pthread_create(&thread, 0, worker, 0);
  pthread_join(thread, 0);
  if (seen[0] != 10) { printf("fresh counter %d\n", seen[0]); ++failures; }
  if (seen[1] != 106) { printf("later+bump %d\n", seen[1]); ++failures; }
  if (counter != 99 || later != 6 || bump() != 102) {
    printf("main %d %d\n", counter, later);
    ++failures;
  }
  if (failures == 0) puts("gnu thread c ok");
  return failures != 0;
}
