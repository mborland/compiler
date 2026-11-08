//type:rp
//options:--c11:--c11 -O2
//use_system_includes: true
//require:BACK_END_TYPE 3
/* <threads.h> is the C library's (glibc): thrd_create and thrd_join, mtx_t,
   call_once and the thread-specific storage functions are plain calls. */
#include <threads.h>

static mtx_t lock;
static once_flag once = ONCE_FLAG_INIT;
static int once_count;
static int total;

static void init_once(void)
{
  once_count++;
}

static int worker(void *arg)
{
  int i;

  call_once(&once, init_once);
  for (i = 0; i < 1000; i++) {
    mtx_lock(&lock);
    total += *(int *)arg;
    mtx_unlock(&lock);
  }
  return *(int *)arg * 2;
}

int main(void)
{
  thrd_t threads[4];
  int args[4] = { 1, 2, 3, 4 };
  int result;
  int failures = 0;
  int i;
  tss_t key;

  if (mtx_init(&lock, mtx_plain) != thrd_success)
    return 100;
  for (i = 0; i < 4; i++)
    if (thrd_create(&threads[i], worker, &args[i]) != thrd_success)
      return 101;
  for (i = 0; i < 4; i++) {
    if (thrd_join(threads[i], &result) != thrd_success)
      return 102;
    if (result != args[i] * 2)
      failures |= 1;
  }
  if (total != 10000)
    failures |= 2;
  if (once_count != 1)
    failures |= 4;
  if (tss_create(&key, 0) != thrd_success)
    return 103;
  tss_set(key, &total);
  if (tss_get(key) != &total)
    failures |= 8;
  tss_delete(key);
  mtx_destroy(&lock);
  return failures;
}
