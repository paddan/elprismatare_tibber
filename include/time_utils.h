#pragma once

#include <Arduino.h>
#include <time.h>

struct PriceState;

// Minimum epoch considered a valid (synced) clock. Dates before 2023-11-11
// indicate the RTC has not been set yet.
constexpr time_t kValidEpochMin = 1700000000;

uint16_t normalizeResolutionMinutes(uint16_t resolutionMinutes);
bool isValidClock(time_t now, time_t validEpochMin);
bool formatDateYmd(time_t ts, char *out, size_t outSize);
String utcIsoToLocalIsoSlot(const String &utcIso);
String intervalKeyFromIso(const String &iso, uint16_t resolutionMinutes);
String currentIntervalKey(uint16_t resolutionMinutes);
int findPricePointIndexForInterval(const PriceState &state, const String &intervalKey, uint16_t resolutionMinutes);
int findCurrentPricePointIndex(const PriceState &state, uint16_t resolutionMinutes);
bool shouldCatchUpMissedDailyUpdate(
    time_t now,
    const PriceState &state,
    int dailyFetchHour,
    int dailyFetchMinute,
    time_t validEpochMin);
const char *timezoneSpecForNordpoolArea(const String &area);
void syncClock(const char *timezoneSpec);
time_t scheduleNextDailyFetch(time_t now, int hour, int minute);
