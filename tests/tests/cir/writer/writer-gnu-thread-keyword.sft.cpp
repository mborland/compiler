//type:rp
//options:--g++:--g++ -O2
//use_system_includes: true
//require:BACK_END_TYPE 3
// The GNU __thread specifier gives a variable thread storage duration, as
// thread_local does: every thread has its own copy, initialized from the
// static initializer, and the main thread's copy is not touched by a thread.
// (The front end records __thread only as a declaration modifier, not as
// is_thread_local; the writer used to send these variables without TLS.)
#include <pthread.h>
#include <stdio.h>

__thread int counter = 10;
static __thread int array[4] = { 1, 2, 3, 4 };
extern __thread int declared_later;
__thread int declared_later = 5;
struct S { static __thread int member; };
__thread int S::member = 7;
thread_local int cxx11 = 20;

static int seen[4];

static int bump()
{
  static __thread int calls = 100;
  return ++calls;
}

static void *worker(void *)
{
  seen[0] = counter;                  // a fresh 10, not main's 99
  counter += 5;
  array[2] = 33;
  declared_later = 50;
  S::member += 1;
  seen[1] = counter + array[2] + declared_later + S::member + cxx11;
  seen[2] = bump() + bump();          // 101 + 102
  seen[3] = array[0];
  return 0;
}

int main()
{
  pthread_t thread;
  int failures = 0;

  counter = 99;
  array[0] = 77;
  declared_later = 6;
  S::member = 8;
  bump();
  pthread_create(&thread, 0, worker, 0);
  pthread_join(thread, 0);
  if (seen[0] != 10) { printf("fresh counter %d\n", seen[0]); ++failures; }
  if (seen[1] != 15 + 33 + 50 + 8 + 20) { printf("sum %d\n", seen[1]); ++failures; }
  if (seen[2] != 203) { printf("bump %d\n", seen[2]); ++failures; }
  if (seen[3] != 1) { printf("array %d\n", seen[3]); ++failures; }
  if (counter != 99 || array[0] != 77 || array[2] != 3 ||
      declared_later != 6 || S::member != 8 || cxx11 != 20) {
    printf("main %d %d %d %d %d %d\n", counter, array[0], array[2], declared_later, S::member, cxx11);
    ++failures;
  }
  if (bump() != 102) { printf("main bump\n"); ++failures; }
  if (failures == 0) puts("gnu thread ok");
  return failures != 0;
}
