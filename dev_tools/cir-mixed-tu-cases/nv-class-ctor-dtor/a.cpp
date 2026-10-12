#include "common.h"

int Counter::live = 0;
int Counter::made = 0;

Counter::Counter() : value(0) { ++live; ++made; }
Counter::Counter(int v) : value(v) { ++live; ++made; }
Counter::Counter(const Counter &other) : value(other.value + 1000) {
  ++live;
  ++made;
}
Counter::~Counter() { --live; }
int Counter::get() const { return value; }
Counter &Counter::add(int v) { value += v; return *this; }

Counter make_counter(int v) {
  Counter c(v);
  c.add(1);
  return c;
}

int sum_array(int n) {
  Counter *items = new Counter[n];
  int total = 0;
  for (int i = 0; i < n; i++) total += items[i].add(i).get();
  delete[] items;
  return total;
}
