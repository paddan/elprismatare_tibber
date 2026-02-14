#include "time_utils.h"

#include "logging_utils.h"

String hourKeyFromIso(const String &iso) {
  if (iso.length() >= 13) return iso.substring(0, 13);
  return "";
}

String currentHourKey() {
  time_t now = time(nullptr);
  if (now < 1700000000) return "";
  struct tm localTm;
  localtime_r(&now, &localTm);
  char key[20];
  strftime(key, sizeof(key), "%Y-%m-%dT%H", &localTm);
  return String(key);
}

void syncClock(const char *timezoneSpec) {
  configTzTime(timezoneSpec, "pool.ntp.org", "time.nist.gov");
  for (int i = 0; i < 20; ++i) {
    if (time(nullptr) > 1700000000) break;
    delay(250);
  }
  logf("Clock sync status: now=%ld", (long)time(nullptr));
}

time_t scheduleNextDailyFetch(time_t now, int hour, int minute) {
  if (now < 1700000000) return 0;

  struct tm tmNow;
  localtime_r(&now, &tmNow);
  tmNow.tm_hour = hour;
  tmNow.tm_min = minute;
  tmNow.tm_sec = 0;

  time_t next = mktime(&tmNow);
  if (next <= now) next += 24 * 3600;
  return next;
}
