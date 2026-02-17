#include "time_utils.h"

#include <ctype.h>
#include <string.h>

#include "app_types.h"
#include "logging_utils.h"

namespace {
constexpr char kTimezoneCetCest[] = "CET-1CEST,M3.5.0/2,M10.5.0/3";
constexpr char kTimezoneEetEest[] = "EET-2EEST,M3.5.0/3,M10.5.0/4";

bool parseTwoDigits(const char *chars, int &out) {
  if (!isdigit((unsigned char)chars[0]) || !isdigit((unsigned char)chars[1])) {
    return false;
  }
  out = ((chars[0] - '0') * 10) + (chars[1] - '0');
  return true;
}

String dateKeyFromTime(time_t when, time_t validEpochMin) {
  if (!isValidClock(when, validEpochMin)) return "";

  struct tm localTm;
  if (!localtime_r(&when, &localTm)) return "";
  char key[11];
  strftime(key, sizeof(key), "%Y-%m-%d", &localTm);
  return String(key);
}

bool stateContainsDate(const PriceState &state, const String &dateKey) {
  if (!state.ok || state.count == 0 || dateKey.length() != 10) return false;

  for (size_t i = 0; i < state.count; ++i) {
    if (state.points[i].startsAt.length() < 10) continue;
    if (state.points[i].startsAt.substring(0, 10) == dateKey) return true;
  }
  return false;
}
}  // namespace

uint16_t normalizeResolutionMinutes(uint16_t resolutionMinutes) {
  if (resolutionMinutes == 15 || resolutionMinutes == 30 || resolutionMinutes == 60) {
    return resolutionMinutes;
  }
  return 60;
}

bool isValidClock(time_t now, time_t validEpochMin) {
  return now > validEpochMin;
}

const char *timezoneSpecForNordpoolArea(const String &area) {
  if (area == "FI" || area == "EE" || area == "LV" || area == "LT") {
    return kTimezoneEetEest;
  }
  return kTimezoneCetCest;
}

String intervalKeyFromIso(const String &iso, uint16_t resolutionMinutes) {
  if (iso.length() < 13) return "";

  const uint16_t normalizedResolution = normalizeResolutionMinutes(resolutionMinutes);
  const char *isoChars = iso.c_str();
  char key[20];

  if (normalizedResolution >= 60 || iso.length() < 16) {
    memcpy(key, isoChars, 13);
    key[13] = '\0';
    return String(key);
  }

  int minute = 0;
  if (!parseTwoDigits(&isoChars[14], minute)) {
    minute = 0;
  }
  const int slotMinute = minute - (minute % normalizedResolution);
  snprintf(key, sizeof(key), "%.13s:%02d", isoChars, slotMinute);
  return String(key);
}

String currentIntervalKey(uint16_t resolutionMinutes) {
  time_t now = time(nullptr);
  if (now < 1700000000) return "";

  struct tm localTm;
  localtime_r(&now, &localTm);

  const uint16_t normalizedResolution = normalizeResolutionMinutes(resolutionMinutes);
  const int slotMinute = localTm.tm_min - (localTm.tm_min % normalizedResolution);
  char key[20];
  if (normalizedResolution >= 60) {
    strftime(key, sizeof(key), "%Y-%m-%dT%H", &localTm);
  } else {
    char hourPrefix[20];
    strftime(hourPrefix, sizeof(hourPrefix), "%Y-%m-%dT%H", &localTm);
    snprintf(key, sizeof(key), "%s:%02d", hourPrefix, slotMinute);
  }
  return String(key);
}

int findPricePointIndexForInterval(const PriceState &state, const String &intervalKey, uint16_t resolutionMinutes) {
  if (intervalKey.isEmpty()) return -1;
  for (size_t i = 0; i < state.count; ++i) {
    if (intervalKeyFromIso(state.points[i].startsAt, resolutionMinutes) == intervalKey) {
      return (int)i;
    }
  }
  return -1;
}

int findCurrentPricePointIndex(const PriceState &state, uint16_t resolutionMinutes) {
  const String key = currentIntervalKey(resolutionMinutes);
  if (key.isEmpty()) return -1;
  return findPricePointIndexForInterval(state, key, resolutionMinutes);
}

bool shouldCatchUpMissedDailyUpdate(
    time_t now,
    const PriceState &state,
    int dailyFetchHour,
    int dailyFetchMinute,
    time_t validEpochMin) {
  if (!isValidClock(now, validEpochMin)) return false;

  struct tm tmToday;
  localtime_r(&now, &tmToday);
  tmToday.tm_hour = dailyFetchHour;
  tmToday.tm_min = dailyFetchMinute;
  tmToday.tm_sec = 0;
  const time_t todayFetchTime = mktime(&tmToday);
  if (todayFetchTime == (time_t)-1 || now < todayFetchTime) return false;

  struct tm tmTomorrow = tmToday;
  tmTomorrow.tm_mday += 1;
  tmTomorrow.tm_hour = 0;
  tmTomorrow.tm_min = 0;
  tmTomorrow.tm_sec = 0;
  const time_t tomorrow = mktime(&tmTomorrow);
  if (!isValidClock(tomorrow, validEpochMin)) return false;

  const String tomorrowDate = dateKeyFromTime(tomorrow, validEpochMin);
  if (tomorrowDate.isEmpty()) return false;

  const bool hasTomorrow = stateContainsDate(state, tomorrowDate);
  if (!hasTomorrow) {
    logf(
        "After %02d:%02d and cache is missing %s, catch-up fetch needed",
        dailyFetchHour,
        dailyFetchMinute,
        tomorrowDate.c_str());
  }
  return !hasTomorrow;
}

void syncClock(const char *timezoneSpec) {
  logf("Clock sync start: tz=%s", timezoneSpec ? timezoneSpec : "(null)");
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
