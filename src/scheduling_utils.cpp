#include "scheduling_utils.h"

time_t scheduleNextMinuteBoundary(time_t now, time_t validEpochMin) {
  if (now <= validEpochMin) return 0;
  return (now - (now % 60)) + 60;
}

time_t scheduleAfter(time_t now, time_t delaySec, time_t validEpochMin) {
  if (now <= validEpochMin || delaySec <= 0) return 0;
  return now + delaySec;
}

