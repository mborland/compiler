//type:rp
//options:--c11:--c11 -O2
//use_system_includes: true
//require:BACK_END_TYPE 3
/* _Thread_local objects: every thread has its own copy, initialized from the
   static initializer; the main thread's copy is not touched by a thread. */
#include <pthread.h>

_Thread_local int counter = 10;
static _Thread_local int array[4] = { 1, 2, 3, 4 };
extern _Thread_local int declared_later;
_Thread_local int declared_later = 5;

static int seen_in_thread[3];

static int bump(void)
{
  static _Thread_local int calls = 100;

  return ++calls;
}

static void *worker(void *unused)
{
  (void)unused;
  seen_in_thread[0] = counter;          /* a fresh 10, not main's 99 */
  counter += 5;
  array[2] = 33;
  declared_later = 50;
  seen_in_thread[1] = counter + array[2] + declared_later;
  seen_in_thread[2] = bump() + bump();  /* 101 + 102 */
  return 0;
}

int main(void)
{
  pthread_t thread;
  int failures = 0;

  counter = 99;
  array[0] = 77;
  bump();
  if (pthread_create(&thread, 0, worker, 0) != 0)
    return 100;
  pthread_join(thread, 0);
  if (seen_in_thread[0] != 10)
    failures |= 1;
  if (seen_in_thread[1] != 15 + 33 + 50)
    failures |= 2;
  if (seen_in_thread[2] != 203)
    failures |= 4;
  if (counter != 99 || array[0] != 77 || array[2] != 3 || declared_later != 5)
    failures |= 8;
  if (bump() != 102)
    failures |= 16;
  return failures;
}
