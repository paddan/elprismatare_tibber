#pragma once

#include <Arduino.h>
#include <time.h>

String formatStartsAt(const String &iso);
String hourKeyFromIso(const String &iso);
String currentHourKey();
void syncClock(const char *timezoneSpec);
time_t scheduleNextDailyFetch(time_t now, int hour, int minute);
