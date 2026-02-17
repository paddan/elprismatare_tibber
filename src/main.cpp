#include <Arduino.h>
#include <WiFi.h>
#include <time.h>

#include "app_types.h"
#include "display_ui.h"
#include "logging_utils.h"
#include "nordpool_client.h"
#include "price_cache.h"
#include "price_state_utils.h"
#include "scheduling_utils.h"
#include "time_utils.h"
#include "wifi_utils.h"

constexpr uint32_t kWifiConnectTimeoutMs = 20000;
constexpr uint16_t kWifiPortalTimeoutSec = 120;
constexpr uint32_t kRetryOnErrorMs = 30000;
constexpr time_t kRetryDailyIfUnchangedSec = 10 * 60;
constexpr uint32_t kResetHoldMs = 2000;
constexpr uint32_t kResetPollIntervalMs = 50;
constexpr int kDailyFetchHour = 13;
constexpr int kDailyFetchMinute = 0;
constexpr char kNordPoolApiUrl[] = "https://dataportal-api.nordpoolgroup.com/api/DayAheadPriceIndices";
constexpr char kActiveSourceLabel[] = "NORDPOOL";
constexpr time_t kValidEpochMin = 1700000000;

#ifndef CONFIG_CLOCK_RESYNC_INTERVAL_SEC
#define CONFIG_CLOCK_RESYNC_INTERVAL_SEC (6 * 60 * 60)
#endif

#ifndef CONFIG_CLOCK_RESYNC_RETRY_SEC
#define CONFIG_CLOCK_RESYNC_RETRY_SEC (10 * 60)
#endif

constexpr time_t kClockResyncIntervalSec =
    (CONFIG_CLOCK_RESYNC_INTERVAL_SEC > 0) ? (time_t)CONFIG_CLOCK_RESYNC_INTERVAL_SEC : (6 * 60 * 60);
constexpr time_t kClockResyncRetrySec =
    (CONFIG_CLOCK_RESYNC_RETRY_SEC > 0) ? (time_t)CONFIG_CLOCK_RESYNC_RETRY_SEC : (10 * 60);

#ifndef CONFIG_RESET_PIN
#define CONFIG_RESET_PIN -1
#endif

#ifndef CONFIG_RESET_ACTIVE_LEVEL
#define CONFIG_RESET_ACTIVE_LEVEL LOW
#endif

PriceState gState;
PriceState gFetchBuffer;
PriceState gCacheBuffer;
AppSecrets gSecrets;
uint32_t gLastFetchMs = 0;
time_t gNextDailyFetch = 0;
time_t gNextMinuteBoundary = 0;
time_t gNextClockResync = 0;
bool gPendingCatchUpRecheck = false;
bool gNeedsOnlineInit = false;

constexpr int kConfigResetPin = CONFIG_RESET_PIN;
constexpr int kConfigResetActiveLevel = CONFIG_RESET_ACTIVE_LEVEL;

bool resetButtonPressed()
{
  if (kConfigResetPin < 0)
    return false;
  return digitalRead(kConfigResetPin) == kConfigResetActiveLevel;
}

bool resetButtonHeld(uint32_t holdMs = kResetHoldMs)
{
  if (!resetButtonPressed())
    return false;

  uint32_t elapsed = 0;
  while (elapsed < holdMs)
  {
    if (!resetButtonPressed())
      return false;
    delay(kResetPollIntervalMs);
    elapsed += kResetPollIntervalMs;
  }
  return true;
}

void handleResetRequest()
{
  if (!resetButtonHeld())
    return;

  logf("Reset button held, clearing WiFi/config settings");
  wifiResetSettings();
  delay(250);
  ESP.restart();
}

void logNextFetch(time_t nextFetch)
{
  if (nextFetch == 0)
    return;
  struct tm tmNext;
  localtime_r(&nextFetch, &tmNext);
  char buf[24];
  strftime(buf, sizeof(buf), "%d/%m %H:%M", &tmNext);
  logf("Next daily fetch scheduled: %s", buf);
}

void scheduleDailyFetch(time_t now)
{
  gNextDailyFetch = scheduleNextDailyFetch(now, kDailyFetchHour, kDailyFetchMinute);
  logNextFetch(gNextDailyFetch);
}

void syncClockForSelectedArea()
{
  const char *timezoneSpec = timezoneSpecForNordpoolArea(gSecrets.nordpoolArea);
  logf("Clock timezone selected: area=%s", gSecrets.nordpoolArea.c_str());
  syncClock(timezoneSpec);
}

void primeSchedulesFromNow(time_t now)
{
  scheduleDailyFetch(now);
  gNextMinuteBoundary = scheduleNextMinuteBoundary(now, kValidEpochMin);
  gNextClockResync = scheduleAfter(now, kClockResyncIntervalSec, kValidEpochMin);
}

void syncClockAndPrimeSchedules()
{
  syncClockForSelectedArea();
  primeSchedulesFromNow(time(nullptr));
}

void applyFetchedState(const PriceState &fetched)
{
  if (fetched.ok)
  {
    gState = fetched;
    if (!priceCacheSave(gState))
    {
      logf("Price cache save failed");
    }
  }
  else if (gState.count > 0)
  {
    gState.error = fetched.error;
  }
  else
  {
    gState = fetched;
  }
  displayDrawPrices(gState);
  gLastFetchMs = millis();
}

void fetchAndRender()
{
  logf("Fetch+render start");
  fetchNordPoolPriceInfo(
      kNordPoolApiUrl,
      gSecrets.nordpoolArea.c_str(),
      gSecrets.nordpoolCurrency.c_str(),
      gSecrets.nordpoolResolutionMinutes,
      gSecrets.vatPercent,
      gSecrets.fixedCostPerKwh,
      gFetchBuffer);
  applyFetchedState(gFetchBuffer);
  logf("Fetch+render done");
}

bool applyLoadedCacheState(const PriceState &cacheState, const char *cacheLabel, bool saveBackToCache)
{
  if (cacheState.resolutionMinutes != gSecrets.nordpoolResolutionMinutes)
  {
    logf(
        "Using %s cache with different resolution: cache=%u configured=%u",
        cacheLabel,
        (unsigned)cacheState.resolutionMinutes,
        (unsigned)gSecrets.nordpoolResolutionMinutes);
  }

  gState = cacheState;
  if (saveBackToCache && !priceCacheSave(gState))
  {
    logf("Price cache save failed");
  }

  displayDrawPrices(gState);
  logf("Loaded %s prices from cache: points=%u", cacheLabel, (unsigned)gState.count);
  gPendingCatchUpRecheck = true;
  return true;
}

void updateCurrentIntervalFromClock(bool forceUpdate = false)
{
  if (!gState.ok || gState.count == 0)
    return;

  const int idx = findCurrentPricePointIndex(gState, gSecrets.nordpoolResolutionMinutes);
  if (idx < 0)
    return;
  if (!forceUpdate && idx == gState.currentIndex)
    return;

  gState.currentIndex = idx;
  gState.currentStartsAt = gState.points[idx].startsAt;
  gState.currentLevel = gState.points[idx].level;
  gState.currentPrice = gState.points[idx].price;
  logf("Price slot update: idx=%d price=%.3f", idx, gState.currentPrice);
  displayDrawPrices(gState);
}

void handleClockDrivenUpdates(time_t now)
{
  time_t currentNow = now;
  if (!isValidClock(currentNow, kValidEpochMin))
    return;

  if (gNextClockResync == 0)
  {
    gNextClockResync = scheduleAfter(currentNow, kClockResyncIntervalSec, kValidEpochMin);
  }
  if (currentNow >= gNextClockResync)
  {
    logf("Periodic clock resync trigger");
    syncClockForSelectedArea();
    const time_t syncedNow = time(nullptr);
    if (isValidClock(syncedNow, kValidEpochMin))
    {
      currentNow = syncedNow;
      displayRefreshClock();
      gNextMinuteBoundary = scheduleNextMinuteBoundary(currentNow, kValidEpochMin);
      gNextClockResync = scheduleAfter(currentNow, kClockResyncIntervalSec, kValidEpochMin);
    }
    else
    {
      gNextClockResync = scheduleAfter(currentNow, kClockResyncRetrySec, kValidEpochMin);
    }
  }

  if (gPendingCatchUpRecheck)
  {
    gPendingCatchUpRecheck = false;
    if (shouldCatchUpMissedDailyUpdate(currentNow, gState, kDailyFetchHour, kDailyFetchMinute, kValidEpochMin))
    {
      gNextDailyFetch = currentNow;
      logf("Delayed catch-up fetch scheduled immediately");
    }
  }

  if (gNextMinuteBoundary == 0)
  {
    gNextMinuteBoundary = scheduleNextMinuteBoundary(currentNow, kValidEpochMin);
  }
  if (currentNow >= gNextMinuteBoundary)
  {
    displayRefreshClock();
    updateCurrentIntervalFromClock();
    gNextMinuteBoundary = scheduleNextMinuteBoundary(currentNow, kValidEpochMin);
  }

  if (gNextDailyFetch == 0)
    scheduleDailyFetch(currentNow);

  if (gNextDailyFetch != 0 && currentNow >= gNextDailyFetch)
  {
    logf("Daily 13:00 fetch trigger");
    fetchNordPoolPriceInfo(
        kNordPoolApiUrl,
        gSecrets.nordpoolArea.c_str(),
        gSecrets.nordpoolCurrency.c_str(),
        gSecrets.nordpoolResolutionMinutes,
        gSecrets.vatPercent,
        gSecrets.fixedCostPerKwh,
        gFetchBuffer);
    const PriceState &fetched = gFetchBuffer;
    if (!fetched.ok)
    {
      logf("Daily fetch failed, retry in %ld sec", (long)kRetryDailyIfUnchangedSec);
      applyFetchedState(fetched);
      gNextDailyFetch = currentNow + kRetryDailyIfUnchangedSec;
      logNextFetch(gNextDailyFetch);
      return;
    }

    if (wouldReduceCoverage(fetched, gState))
    {
      logf(
          "Daily fetch has fewer prices (%u < %u), keep existing and retry in %ld sec",
          (unsigned)fetched.count,
          (unsigned)gState.count,
          (long)kRetryDailyIfUnchangedSec);
      gNextDailyFetch = currentNow + kRetryDailyIfUnchangedSec;
      logNextFetch(gNextDailyFetch);
      return;
    }

    if (hasNewPriceInfo(fetched, gState))
    {
      logf("Daily fetch returned updated prices");
      applyFetchedState(fetched);
      scheduleDailyFetch(currentNow);
      return;
    }

    logf("Daily fetch unchanged, retry in %ld sec", (long)kRetryDailyIfUnchangedSec);
    gNextDailyFetch = currentNow + kRetryDailyIfUnchangedSec;
    logNextFetch(gNextDailyFetch);
  }
}

void setup()
{
  Serial.begin(115200);
  delay(200);
  logf("Boot");
  logf(
      "Clock resync config: interval=%ld sec retry=%ld sec",
      (long)kClockResyncIntervalSec,
      (long)kClockResyncRetrySec);

  if (kConfigResetPin >= 0)
  {
    if (kConfigResetActiveLevel == LOW)
      pinMode(kConfigResetPin, INPUT_PULLUP);
    else
      pinMode(kConfigResetPin, INPUT_PULLDOWN);
  }

  handleResetRequest();

  displayInit();
  loadAppSecrets(gSecrets);

  bool loadedFromCache = false;
  const bool wifiConnected = wifiConnectWithConfigPortal(gSecrets, kWifiPortalTimeoutSec);

  if (!wifiConnected)
  {
    if (priceCacheLoadIfAvailable(kActiveSourceLabel, gCacheBuffer))
    {
      gState = gCacheBuffer;
      gState.source = "no wifi";
      displayDrawPrices(gState);
      updateCurrentIntervalFromClock(true);
      logf("No WiFi at boot, loaded prices from cache: points=%u", (unsigned)gState.count);
      gNeedsOnlineInit = true;
      return;
    }

    gState.ok = false;
    gState.source = "no wifi";
    gState.error = "no wifi";
    displayDrawPrices(gState);
    gNeedsOnlineInit = true;
    return;
  }

  syncClockAndPrimeSchedules();

  if (priceCacheLoadIfCurrent(kActiveSourceLabel, gCacheBuffer))
  {
    loadedFromCache = applyLoadedCacheState(gCacheBuffer, "current", true);
  }
  else if (priceCacheLoadIfAvailable(kActiveSourceLabel, gCacheBuffer))
  {
    loadedFromCache = applyLoadedCacheState(gCacheBuffer, "available", false);
  }

  if (!loadedFromCache)
  {
    fetchAndRender();
  }

  const time_t now = time(nullptr);
  if (loadedFromCache && shouldCatchUpMissedDailyUpdate(now, gState, kDailyFetchHour, kDailyFetchMinute, kValidEpochMin))
  {
    gNextDailyFetch = now;
    logf("Startup catch-up fetch scheduled immediately");
    gPendingCatchUpRecheck = false;
  }

  updateCurrentIntervalFromClock(true);
}

void loop()
{
  handleResetRequest();

  if (WiFi.status() != WL_CONNECTED && !wifiReconnect(kWifiConnectTimeoutMs))
  {
    if (gState.ok)
    {
      if (gState.source != "no wifi")
      {
        gState.source = "no wifi";
        displayDrawPrices(gState);
      }
    }
    else
    {
      const bool needsRedraw = gState.source != "no wifi" || gState.error != "no wifi";
      gState.source = "no wifi";
      gState.error = "no wifi";
      if (needsRedraw)
      {
        displayDrawPrices(gState);
      }
    }
    return;
  }

  if (gNeedsOnlineInit && WiFi.status() == WL_CONNECTED)
  {
    logf("WiFi restored, running online init");
    gNeedsOnlineInit = false;
    loadAppSecrets(gSecrets);
    syncClockAndPrimeSchedules();
    fetchAndRender();
  }

  if (!gState.ok && millis() - gLastFetchMs >= kRetryOnErrorMs)
  {
    logf("Retry fetch due to error state");
    fetchAndRender();
  }

  handleClockDrivenUpdates(time(nullptr));
}
