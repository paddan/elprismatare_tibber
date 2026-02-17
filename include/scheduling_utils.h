#pragma once

#include <time.h>

time_t scheduleNextMinuteBoundary(time_t now, time_t validEpochMin);
time_t scheduleAfter(time_t now, time_t delaySec, time_t validEpochMin);

