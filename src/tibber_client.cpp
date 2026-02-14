#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "logging_utils.h"
#include "tibber_client.h"
#include "time_utils.h"

void addPoints(JsonArray arr, PriceState &state) {
  if (arr.isNull()) return;
  for (JsonObject item : arr) {
    if (state.count >= kMaxPoints) return;
    PricePoint &p = state.points[state.count++];
    p.startsAt = String((const char *)(item["startsAt"] | ""));
    p.level = String((const char *)(item["level"] | "UNKNOWN"));
    const float energy = item["energy"] | 0.0f;
    const float tax = item["tax"] | 0.0f;
    const float total = item["total"] | 0.0f;
    p.price = (energy + tax) > 0.0f ? (energy + tax) : total;
  }
}

PriceState fetchPriceInfo(const char *apiToken, const char *graphQlUrl) {
  PriceState out;
  logf("PriceInfo fetch start. free_heap=%u", ESP.getFreeHeap());

  if (apiToken == nullptr || strlen(apiToken) == 0) {
    out.error = "Missing TIBBER_API_TOKEN";
    return out;
  }
  if (WiFi.status() != WL_CONNECTED) {
    out.error = "WiFi not connected";
    return out;
  }

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setConnectTimeout(10000);
  http.setTimeout(10000);
  if (!http.begin(client, graphQlUrl)) {
    out.error = "HTTP begin failed";
    return out;
  }

  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " + String(apiToken));

  static const char kBody[] =
      "{\"query\":\"{viewer{homes{currentSubscription{priceInfo{current{total energy tax startsAt "
      "currency level} today{total energy tax startsAt level} tomorrow{total energy tax startsAt "
      "level}}}}}}\"}";

  const int status = http.POST(String(kBody));
  logf("Tibber POST status=%d", status);
  if (status != 200) {
    out.error = status <= 0 ? "HTTP POST failed" : ("HTTP " + String(status));
    http.end();
    return out;
  }

  JsonDocument doc;
  WiFiClient *stream = http.getStreamPtr();
  const DeserializationError err = deserializeJson(doc, *stream);
  http.end();
  if (err) {
    out.error = "JSON parse failed";
    logf("JSON parse error: %s", err.c_str());
    return out;
  }
  if (!doc["errors"].isNull()) {
    out.error = "Tibber API error";
    return out;
  }

  JsonObject current = doc["data"]["viewer"]["homes"][0]["currentSubscription"]["priceInfo"]["current"];
  if (current.isNull()) {
    out.error = "No current tariff";
    return out;
  }

  out.currency = String((const char *)(current["currency"] | "SEK"));
  out.currentStartsAt = String((const char *)(current["startsAt"] | ""));
  out.currentLevel = String((const char *)(current["level"] | "UNKNOWN"));
  const float currentEnergy = current["energy"] | 0.0f;
  const float currentTax = current["tax"] | 0.0f;
  const float currentTotal = current["total"] | 0.0f;
  out.currentPrice = (currentEnergy + currentTax) > 0.0f ? (currentEnergy + currentTax) : currentTotal;

  addPoints(doc["data"]["viewer"]["homes"][0]["currentSubscription"]["priceInfo"]["today"], out);
  addPoints(doc["data"]["viewer"]["homes"][0]["currentSubscription"]["priceInfo"]["tomorrow"], out);

  if (out.count == 0) {
    out.error = "No hourly prices";
    return out;
  }

  out.currentIndex = -1;
  for (size_t i = 0; i < out.count; ++i) {
    if (out.points[i].startsAt == out.currentStartsAt) {
      out.currentIndex = (int)i;
      break;
    }
  }

  if (out.currentIndex < 0) {
    const String key = currentHourKey();
    for (size_t i = 0; i < out.count; ++i) {
      if (hourKeyFromIso(out.points[i].startsAt) == key) {
        out.currentIndex = (int)i;
        break;
      }
    }
  }

  out.ok = true;
  logf(
      "PriceInfo OK: points=%u current=%.3f %s level=%s",
      (unsigned)out.count,
      out.currentPrice,
      out.currency.c_str(),
      out.currentLevel.c_str()
  );
  return out;
}
