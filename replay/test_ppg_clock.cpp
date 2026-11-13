#include "../HealthWatch/src/PPGClock.h"
#include <cassert>

int main() {
  PPGClock clock;
  clock.startBatch(20000, 2);
  assert(clock.next() == 10000);
  assert(clock.next() == 20000);
  // Host scheduling jitter and a three-sample batch must not duplicate 20000.
  clock.startBatch(40000, 3);
  const uint32_t first = clock.next();
  assert(first >= 29500 && first <= 30500);
  assert(clock.next() == first + 10000);
  assert(clock.next() == first + 20000);
  clock.reset();
  clock.startBatch(100000, 1);
  assert(clock.next() == 100000);
}
