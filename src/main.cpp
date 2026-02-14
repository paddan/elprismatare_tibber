#include <Arduino.h>
#include <WiFi.h>
#include <time.h>

#include "app_types.h"
#include "display_ui.h"
#include "logging_utils.h"
#include "tibber_client.h"
#include "time_utils.h"
#include "wifi_utils.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "Missing include/secrets.h. Copy include/secrets.example.h to include/secrets.h and set credentials."
#endif

constexpr uint32_t kWifiConnectTimeoutMs = 20000;
constexpr uint32_t kRetryOnErrorMs = 30000;
constexpr char kTibberGraphQlUrl[] = "https://api.tibber.com/v1-beta/gql";
constexpr char kTimezoneSpec[] = "CET-1CEST,M3.5.0/2,M10.5.0/3";
constexpr bool kTokenMissing = (sizeof(TIBBER_API_TOKEN) <= 1);

PriceState gState;
uint32_t gLastFetchMs = 0;
time_t gNextDailyFetch = 0;
uint32_t gLastMinuteTick = 0;

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

void scheduleDailyFetch()
{
  gNextDailyFetch = scheduleNextDailyFetch(time(nullptr), 13, 0);
  logNextFetch(gNextDailyFetch);
}

void applyFetchedState(const PriceState &fetched)
{
  if (fetched.ok)
  {
    gState = fetched;
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
  applyFetchedState(fetchPriceInfo(TIBBER_API_TOKEN, kTibberGraphQlUrl));
  logf("Fetch+render done");
}

void updateCurrentHourFromClock()
{
  if (!gState.ok || gState.count == 0)
    return;

  const String key = currentHourKey();
  if (key.isEmpty())
    return;

  int idx = -1;
  for (size_t i = 0; i < gState.count; ++i)
  {
    if (hourKeyFromIso(gState.points[i].startsAt) == key)
    {
      idx = (int)i;
      break;
    }
  }
  if (idx < 0 || idx == gState.currentIndex)
    return;

  gState.currentIndex = idx;
  gState.currentStartsAt = gState.points[idx].startsAt;
  gState.currentLevel = gState.points[idx].level;
  gState.currentPrice = gState.points[idx].price;
  logf("Hour change update: idx=%d price=%.3f", idx, gState.currentPrice);
  displayDrawPrices(gState);
}

void setup()
{
  Serial.begin(115200);
  delay(200);
  logf("Boot");

  displayInit();
  displayDrawStaticUi();

  if (kTokenMissing)
  {
    gState.ok = false;
    gState.error = "Set token in include/secrets.h";
    displayDrawPrices(gState);
    return;
  }

  if (!wifiConnect(WIFI_SSID, WIFI_PASSWORD, kWifiConnectTimeoutMs))
  {
    gState.ok = false;
    gState.error = "WiFi timeout";
    displayDrawPrices(gState);
    return;
  }

  syncClock(kTimezoneSpec);
  scheduleDailyFetch();
  fetchAndRender();
}

void loop()
{
  if (WiFi.status() != WL_CONNECTED && !wifiConnect(WIFI_SSID, WIFI_PASSWORD, kWifiConnectTimeoutMs))
  {
    return;
  }

  if (!gState.ok && millis() - gLastFetchMs >= kRetryOnErrorMs)
  {
    logf("Retry fetch due to error state");
    fetchAndRender();
  }

  if (time(nullptr) > 1700000000)
  {
    const uint32_t minuteTick = (uint32_t)(time(nullptr) / 60);
    if (minuteTick != gLastMinuteTick)
    {
      gLastMinuteTick = minuteTick;
      updateCurrentHourFromClock();
    }

    if (gNextDailyFetch == 0)
      scheduleDailyFetch();
    if (gNextDailyFetch != 0 && time(nullptr) >= gNextDailyFetch)
    {
      logf("Daily 13:00 fetch trigger");
      fetchAndRender();
      scheduleDailyFetch();
    }
  }
}
